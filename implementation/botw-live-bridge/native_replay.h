// Offline replay calls production geometry/cache/publication code. No guest or game process.
#pragma once
// Production protocol11 ring with a simulated MC consumer: acknowledges by
// advancing the tail and counts every clear the native publisher emits.
struct ReplayRing {
    std::vector<uint8_t> memory;uint64_t consumed=0,clears=0,regions=0;
    void attach(){
        memory.assign(botwcraft::proto::kOffCollisionRing+botwcraft::proto::kCollisionRingBytes,0);
        live::base=memory.data();live::connected=true;live::failed=false;
    }
    void consume(){
        using namespace botwcraft::proto;
        auto ring=live::base+kOffCollisionRing;uint64_t head=*reinterpret_cast<uint64_t*>(ring);
        while(consumed<head){
            uint64_t pos=consumed%kColRingDataBytes;ColMsgHeader h;memcpy(&h,ring+0x80+pos,8);
            if(h.type==kColPad&&!h.payloadBytes){consumed+=kColRingDataBytes-pos;continue;}
            if(h.type==kColClear)++clears;if(h.type==kColRegion)++regions;
            consumed+=(8ull+h.payloadBytes+7)&~7ull;
        }
        *reinterpret_cast<uint64_t*>(ring+0x40)=consumed;
    }
};
struct WipeCounts {uint64_t expiration=0,distance=0,replacement=0,timeout=0,lostNeighbour=0,lostAfterLoss=0;};
// Published-region lifecycle on real native geometry: complete regions are
// published and acknowledged while the player moves. Only a genuine epoch
// change may clear Minecraft collision; refresh age, eviction, incomplete or
// timed-out replacement must not. Coverage loss must pause, not wipe.
static void publishedLifecycle(const std::vector<botwcraft::proto::ColTri>& geometry,const double* origin,WipeCounts& w,size_t& jobs,size_t& evictions){
    ReplayRing ring;ring.attach();
    auto& cache=nativeRegions::cache;double velocity[]{6,0,0},p[3];uint64_t now=10000,base=0;
    auto serve=[&](bool complete){
        auto job=cache.next(p,velocity,now);if(!job)return false;++jobs;
        std::vector<botwcraft::proto::ColTri> region;for(const auto& t:geometry)if(nativeGeometry::overlaps(t,job->lo,job->hi))region.push_back(t);
        assert(cache.finish(*job,complete,std::move(region),now+1));
        for(int i=0;i<64;i++){nativeStream::publish(now+1);ring.consume();}
        return true;
    };
    auto clears=[&]{nativeStream::publish(now);ring.consume();return ring.clears-base;};
    double lo[3],hi[3];
    auto body=[&]{for(int k=0;k<3;k++){lo[k]=p[k]-(k==1?0:.3);hi[k]=p[k]+(k==1?1.8:.3);}};
    // Each scenario starts from a fresh epoch whose 5^3 neighbourhood was
    // acquired, published and acknowledged within 125 ms (no ageing).
    auto fill=[&]{
        std::copy_n(origin,3,p);now+=100000;nativeStream::reset(5,9,0x1234,"replay");
        for(int i=0;i<125&&serve(true);i++)now+=1;
        base=ring.clears;body();if(!cache.covers(lo,hi,now,ring.consumed))++w.lostNeighbour;
    };
    // 1) Age alone beyond the old 750 ms refresh deadline.
    fill();now+=800;if(clears())++w.expiration;
    // 2) A published region's refresh returns incomplete.
    fill();now+=760;if(auto job=cache.next(p,velocity,now)){++jobs;assert(cache.finish(*job,false,{},now+1));}
    if(clears())++w.replacement;
    // 3) A refresh times out in flight.
    fill();now+=760;if(cache.next(p,velocity,now)){++jobs;now+=1001;cache.next(p,velocity,now);}
    if(clears())++w.timeout;
    // A valid neighbour remains covered after an incomplete/timed-out refresh.
    if(!cache.covers(lo,hi,now,ring.consumed))++w.lostNeighbour;
    // 4) Fast travel (no ageing) evicts published regions by distance.
    fill();auto evictedBefore=cache.evicted;
    for(int step=1;step<=4;step++){p[0]+=8;for(int i=0;i<6&&serve(true);i++)now+=1;}
    evictions+=cache.evicted-evictedBefore;assert(cache.evicted>evictedBefore&&cache.entries.size()<=343);
    if(clears())++w.distance;
    // 5) Coverage loss: nothing refreshes for a long time. Movement must pause
    // (covers false) while Minecraft keeps its last valid collision (no clear).
    fill();now+=5000;body();
    if(cache.covers(lo,hi,now,ring.consumed))++w.lostAfterLoss;
    if(clears())++w.expiration;
    live::base=nullptr;
}
// Real-geometry survey of the native body-origin conversion: stand a
// TriCollider footprint on every grid point of the snapshot and classify it.
struct SupportSurvey {uint64_t points=0,plane=0,unexplained=0,flat=0;double maxGap=0,sumGap=0;};
static void supportSurvey(const std::vector<botwcraft::proto::ColTri>& geometry,const double* origin,SupportSurvey& out){
    NativeSupport s;for(const auto& t:geometry){std::array<float,9> v;std::copy_n(t.v,9,v.begin());s.tris.push_back(v);}
    for(int i=-12;i<=12;i++)for(int j=-12;j<=12;j++){
        double x=origin[0]+i*.25+.0137,z=origin[2]+j*.25+.0071;
        double y=support::floor(s,x,origin[1]+3,z,0,false);
        if(!std::isfinite(y))continue;
        auto r=support::project(s,x,y,z,true,.03/nativeRegions::scale);++out.points;
        if(r.kind==support::Plane){++out.plane;if(r.gap<1e-9)++out.flat;out.maxGap=std::max(out.maxGap,r.gap);out.sumGap+=r.gap;}
        else ++out.unexplained;
    }
}
static int nativeReplay(const char* path){
    std::ifstream f(path,std::ios::binary);auto read=[&](auto& v){f.read(reinterpret_cast<char*>(&v),sizeof(v));assert(f);};
    uint32_t magic=0,count=0;read(magic);read(count);assert(magic==0x3150524e&&count&&count<=256);
    size_t triangles=0,terrainTriangles=0,voxelBlocks=0,jobs=0,evictions=0;WipeCounts wipes;SupportSurvey survey;
    for(uint32_t sample=0;sample<count;sample++){
        uint32_t n;read(n);assert(n<=4096);std::vector<botwcraft::proto::ColTri> geometry;
        for(uint32_t i=0;i<n;i++){
            botwcraft::proto::ColTri t{};for(int k=0;k<9;k++){read(t.v[k]);t.v[k]=(t.v[k]-nativeRegions::anchor[k%3])/nativeRegions::scale+nativeRegions::origin[k%3];}
            assert(nativeGeometry::valid(t));geometry.push_back(t);
        }
        triangles+=n;
        nativeShapes::TerrainGrid grid;uint32_t complete;read(complete);
        read(grid.x);read(grid.z);read(grid.width);read(grid.depth);read(grid.offset);read(grid.spacing);read(grid.count);
        assert(grid.count<=grid.height.size());
        for(uint32_t i=0;i<grid.count;i++)read(grid.height[i]);
        grid.complete=complete!=0;
        if(grid.complete){assert(grid.width>=2&&grid.depth>=2&&grid.count==uint32_t(grid.width*grid.depth));for(uint32_t i=0;i<grid.count;i++)assert(std::isfinite(grid.height[i]));}
        size_t before=geometry.size();nativeShapes::terrainTriangles(grid,geometry);terrainTriangles+=geometry.size()-before;
        for(const auto& t:geometry)assert(nativeGeometry::valid(t));
        // Voxelize one region containing this real snapshot's geometry.
        double origin[]{0,100,0};if(!geometry.empty())for(int k=0;k<3;k++)origin[k]=(geometry[0].v[k]+geometry[0].v[k+3]+geometry[0].v[k+6])/3.;
        auto key=nativeRegions::Cache::key(origin);float lo[3],hi[3];for(int k=0;k<3;k++){lo[k]=key[k]*8-.5f;hi[k]=key[k]*8+8.5f;}
        std::vector<botwcraft::proto::ColTri> nearby;for(const auto& t:geometry)if(nativeGeometry::overlaps(t,lo,hi))nearby.push_back(t);
        std::vector<botwcraft::proto::ColBlock> blocks;assert(nativeGeometry::voxelize(key,nearby,blocks));voxelBlocks+=blocks.size();
        // Unknown and stale work is never covered.
        nativeRegions::Cache cache;cache.reset(2,7,0x1234);double velocity[]{5,0,0};uint64_t now=1000;
        for(int step=-6;step<=6;step++){
            double p[]{origin[0]+step*8.1,origin[1]+(step%3)*8.1,origin[2]-step*8.1};
            auto job=cache.next(p,velocity,now);assert(job);++jobs;
            std::vector<botwcraft::proto::ColTri> region;for(const auto& t:geometry)if(nativeGeometry::overlaps(t,job->lo,job->hi))region.push_back(t);
            assert(cache.finish(*job,false,std::move(region),now+1));double head[]{p[0]+.3,p[1]+1.8,p[2]+.3};
            assert(!cache.covers(p,head,now+2,UINT64_MAX));assert(cache.entries.size()<=343);now+=100;
        }
        assert(cache.evicted>0);evictions+=cache.evicted;
        auto stale=cache.next(origin,velocity,now);assert(stale);cache.reset(3,8,0x5678);
        assert(!cache.finish(*stale,false,{},now+1));
        publishedLifecycle(geometry,origin,wipes,jobs,evictions);
        supportSurvey(geometry,origin,survey);
    }
    assert(f.peek()==std::char_traits<char>::eof());
    printf("Published lifecycle global wipes: expiration=%llu distance=%llu incomplete_replacement=%llu timeout=%llu; lost_neighbour=%llu; loss_not_reported=%llu\n",
        wipes.expiration,wipes.distance,wipes.replacement,wipes.timeout,wipes.lostNeighbour,wipes.lostAfterLoss);
    printf("Support projection on real geometry: %llu standing points, %llu one-plane (%llu flat), %llu kept MC feet; gap mean %.6g max %.6g MC (native max %.6g)\n",
        survey.points,survey.plane,survey.flat,survey.unexplained,survey.plane?survey.sumGap/survey.plane:0.,survey.maxGap,survey.maxGap*nativeRegions::scale);
    bool pass=!wipes.expiration&&!wipes.distance&&!wipes.replacement&&!wipes.timeout&&!wipes.lostNeighbour&&!wipes.lostAfterLoss;
    printf("%s replay: %u snapshots, %zu native triangles, %zu terrain triangles, %zu voxel blocks; %zu jobs, %zu evictions; unknown/stale rejected\n",pass?"PASS":"FAIL",count,triangles,terrainTriangles,voxelBlocks,jobs,evictions);
    return pass?0:1;
}
