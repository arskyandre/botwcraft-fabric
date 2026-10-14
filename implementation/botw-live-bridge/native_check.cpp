// Host check of the production nested-state and lock bookkeeping; no game.
#include "probe.cpp"
#include <cassert>
#include <set>
#include "native_replay.h"
int main(int argc,char** argv){
    if(argc==3&&std::string(argv[1])=="--replay")return nativeReplay(argv[2]);
    if(argc==4&&std::string(argv[1])=="--mesh"){
        // Production decoder on a live capture, against the game's own triangles.
        auto slurp=[](const char* path){std::ifstream f(path,std::ios::binary);return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)),std::istreambuf_iterator<char>());};
        auto dump=slurp(argv[2]),expect=slurp(argv[3]);nativeMesh::Image memory;uint32_t magic=0;
        assert(dump.size()>=4&&(memcpy(&magic,dump.data(),4),magic==0x314D4B48));
        for(size_t at=4;at+8<=dump.size();){uint32_t address,size;memcpy(&address,dump.data()+at,4);memcpy(&size,dump.data()+at+4,4);at+=8;
            memory.ranges.emplace_back(address,std::vector<uint8_t>(dump.begin()+at,dump.begin()+at+size));at+=size;}
        auto read=[&](uint32_t address,uint32_t size,uint8_t* out){auto p=memory.at(address,size);if(p)memcpy(out,p,size);return p!=nullptr;};
        const uint8_t* e=expect.data();uint32_t roots;memcpy(&magic,e,4);memcpy(&roots,e+4,4);e+=8;assert(magic==0x3158454D);
        nativeMesh::Comparison total;uint32_t decoded=0,failed=0;uint64_t bytes=0;
        for(uint32_t i=0;i<roots;i++){
            uint32_t head[3],count;float xf[16],lo[3],hi[3];memcpy(head,e,12);memcpy(xf,e+12,64);memcpy(lo,e+76,12);memcpy(hi,e+88,12);memcpy(&count,e+100,4);e+=104;
            std::vector<nativeMesh::Keyed> native(count),mine;for(auto& t:native){memcpy(&t.key,e,4);memcpy(t.v,e+4,36);e+=40;}
            nativeMesh::Root root;
            if(!nativeMesh::capture(root,head[0],head[1],read)||!nativeMesh::region(root,xf,lo,hi,mine)){++failed;continue;}
            bytes+=root.image.bytes;decoded+=uint32_t(mine.size());auto r=nativeMesh::compare(mine,native,lo,hi,head[2]!=0);
            total.matched+=r.matched;total.missing+=r.missing;total.moved+=r.moved;total.extra+=r.extra;
        }
        printf("%s mesh decode: %u roots, %u undecodable, %llu bytes copied, %u decoded in box; matched %u missing %u moved %u extra %u\n",
            total.ok()&&!failed&&total.matched?"PASS":"FAIL",roots,failed,(unsigned long long)bytes,decoded,total.matched,total.missing,total.moved,total.extra);
        return total.ok()&&!failed&&total.matched?0:1;
    }
    char temp[MAX_PATH],name[MAX_PATH];assert(GetTempPathA(MAX_PATH,temp)&&GetTempFileNameA(temp,"gtr",0,name));
    std::string tracePath=name;FILE* testTrace=fopen(name,"w");assert(testTrace);uint64_t rotations=0;
    for(int i=0;i<40;i++){assert(trace::room(testTrace,tracePath,"header\n",rotations,64));fputs("row0123456789\n",testTrace);}
    fclose(testTrace);assert(rotations>2);
    for(const auto& suffix:{"",".1",".2"}){
        auto p=tracePath+suffix;std::ifstream input(p,std::ios::binary|std::ios::ate);assert(input&&input.tellg()<90);input.close();assert(DeleteFileA(p.c_str()));
    }
    guest=static_cast<uint8_t*>(VirtualAlloc(nullptr,0x10500000,MEM_RESERVE,PAGE_READWRITE));
    assert(guest&&VirtualAlloc(guest,0x20000,MEM_COMMIT,PAGE_READWRITE));
    assert(VirtualAlloc(guest+0x10000000,0x10000,MEM_COMMIT,PAGE_READWRITE));
    getGuest=[]()->void*{return guest;};body::base=guest;
    CPU outer{};outer.gpr[1]=0x10000;outer.spr.lr=123;
    for(int i=0;i<64;i++)outer.fpr[i]=i+.25;
    outer.fpscr=0x12345678;outer.spr.xer=0xaabbccdd;outer.ca=1;outer.lsqe=1;outer.pse=1;
    for(int i=0;i<8;i++)outer.spr.gqr[i]=17+i;
    CPU expected=outer;
    calibration::begin=1;calibration::running.id=1;calibration::running.epoch=body::lifecycleEpoch.load();
    nativeShapes::enter(&outer);nativeShapes::locked(&outer);
    assert(nativeShapes::harvesting(&outer));
    for(double& v:outer.fpr)v=-99;
    outer.fpscr=0;outer.spr.xer=0;outer.ca=0;outer.lsqe=0;outer.pse=0;
    memset(outer.spr.gqr,0,sizeof(outer.spr.gqr));
    CPU nested=outer;nested.gpr[1]=0x11000;nativeShapes::enter(&nested);nativeShapes::locked(&nested);
    nested.fpr[1]=777;nested.spr.gqr[4]=999;
    nativeShapes::keys(&nested);nativeShapes::finish(&nested);
    assert(nested.fpr[1]==-99&&nested.spr.gqr[4]==0);
    assert(nativeShapes::harvesting(&outer)&&nativeShapes::state.load()==1);
    nativeShapes::finish(&outer);
    assert(!memcmp(outer.fpr,expected.fpr,sizeof(outer.fpr))&&outer.fpscr==expected.fpscr);
    assert(outer.spr.xer==expected.spr.xer&&!memcmp(outer.spr.gqr,expected.spr.gqr,sizeof(outer.spr.gqr)));
    assert(outer.ca==1&&outer.lsqe==1&&outer.pse==1&&nativeShapes::state.load()==2);
    outer.gpr[31]=0x1234;nativeShapes::lockEntered(&outer);nativeShapes::lockEntered(&outer);
    assert(nativeShapes::worldLocks[0].depth==2);
    nativeShapes::lockLeft(&outer);assert(nativeShapes::worldLocks[0].depth==1);
    nativeShapes::lockLeft(&outer);nativeShapes::lockLeft(&outer);assert(!nativeShapes::worldLocks[0].depth);
    nativeRegions::Cache cache;cache.reset(2,7);double p[]{0,100,0},v[]{5,0,0};
    auto a=cache.next(p,v,1000);assert(a&&a->key==nativeRegions::Key({0,12,0}));
    assert(!cache.next(p,v,1001));
    assert(cache.finish(*a,false,{},1002));
    double lo[]{0,100,0},hi[]{.5,102,.5};assert(!cache.covers(lo,hi,1003,999));
    cache.reset(3,7);assert(!cache.finish(*a,true,{},1004));
    a=cache.next(p,v,1005);assert(a&&cache.finish(*a,true,{},1006));
    assert(!cache.covers(lo,hi,1007,999));
    cache.entries[a->key].stableHead=100;assert(!cache.covers(lo,hi,1007,99));
    assert(cache.covers(lo,hi,1007,100)); // Known empty differs from unknown.
    hi[1]=112;assert(!cache.covers(lo,hi,1007,100)); // Head volume, not XZ ground.
    hi[1]=102;assert(cache.covers(lo,hi,1800,100)&&!cache.covers(lo,hi,1006+nativeRegions::kMaxAgeMs,100)); // Age pauses, it does not clear.
    double badPosition[]{INFINITY,100,0};assert(!cache.next(badPosition,v,1800));
    double enormous[]{1e30,100,0};assert(!cache.covers(enormous,hi,1800,100));
    p[0]=800;a=cache.next(p,v,1800);assert(cache.evicted==1&&cache.entries.empty());
    cache.next(p,v,2801);assert(cache.timeouts==1);
    using namespace botwcraft::proto;
    // The scheduler serves missing or older neighbours first. A test about one
    // region answers the others (complete and empty, or unknown) until its turn.
    const nativeRegions::Key home{0,12,0};
    auto turn=[](nativeRegions::Cache& c,const double* at,const double* speed,uint64_t t,nativeRegions::Key key,bool complete){
        for(;;){auto j=c.next(at,speed,t);assert(j);if(j->key==key)return j;assert(c.finish(*j,complete,{},t));}
    };
    ColTri floor{{0,100.25f,0,0,100.25f,8,8,100.25f,0},0};
    ColTri outside{{9,100,0,9,101,0,9,100,1},0};
    float rlo[]{0,96,0},rhi[]{8,104,8};
    assert(nativeGeometry::valid(floor)&&nativeGeometry::overlaps(floor,rlo,rhi));
    assert(!nativeGeometry::overlaps(outside,rlo,rhi));
    // AABB intersects but the triangle is wholly beyond the box's diagonal.
    ColTri corner{{7,100,10,10,100,7,10,100,10},0};
    assert(!nativeGeometry::overlaps(corner,rlo,rhi));
    std::vector<ColBlock> voxels;
    assert(nativeGeometry::voxelize({0,12,0},{floor},voxels)&&!voxels.empty());
    auto voxel=std::find_if(voxels.begin(),voxels.end(),[](const auto& b){return b.x==0&&b.y==100&&b.z==0;});
    assert(voxel!=voxels.end()&&voxel->bits[1]&&voxel->bits[2]);
    ColTri wall{{1,96,0,1,104,0,1,96,8},0};
    assert(nativeGeometry::voxelize({0,12,0},{wall},voxels)&&!voxels.empty());
    assert(nativeGeometry::voxelize({0,12,0},{},voxels)&&voxels.empty());
    ColTri invalid=floor;invalid.v[0]=NAN;assert(!nativeGeometry::voxelize({0,12,0},{invalid},voxels));
    static nativeShapes::Snapshot terrainSnapshot;
    nativeShapes::Block shape,transform;shape.address=0x10000000;shape.size=0xc0;transform.size=64;
    auto putWord=[](auto& block,int offset,uint32_t value){value=__builtin_bswap32(value);memcpy(block.bytes.data()+offset,&value,4);};
    auto putFloat=[&](auto& block,int offset,float value){uint32_t bits;memcpy(&bits,&value,4);putWord(block,offset,bits);};
    // Production inventory must include all three island groups, reject a bad
    // count or duplicate member, and retain rotated body-space geometry.
    nativeShapes::Block world;world.size=256;world.address=0x10000000;
    putWord(world,0x20,0x10000100);
    for(int group=0;group<3;group++){
        auto island=0x10000100+group*0x100,ptr=0x10001000+group*0x100,entity=0x10002000+group*0x100;
        body::put32(island+0x4c,ptr);body::put32(island+0x50,1);body::put32(island+0x54,1);body::put32(ptr,entity);
        body::put32(entity,0x10409fa8);body::put32(entity+8,world.address);body::put32(entity+0x10,0x10003000);body::put32(entity+0x18,0x10004000);
        if(group){int offset=group==1?0x28:0x34;putWord(world,offset,ptr+4);putWord(world,offset+4,1);putWord(world,offset+8,1);body::put32(ptr+4,island);}
    }
    body::put32(0x10003000,0x1040da74);
    for(int k=0;k<3;k++)body::putf(0x10004000+k*20,1);
    auto& snapshot=nativeShapes::pending;snapshot.world=world.address;snapshot.candidateCount=snapshot.entityTotal=0;
    LARGE_INTEGER now;QueryPerformanceCounter(&now);snapshot.qpc=now.QuadPart;
    assert(nativeShapes::inventory(snapshot,world)&&snapshot.candidateCount==3&&snapshot.bodies[0]==1&&snapshot.bodies[1]==1&&snapshot.bodies[2]==1);
    snapshot.candidateCount=0;putWord(world,0x2c,2);assert(!nativeShapes::inventory(snapshot,world));putWord(world,0x2c,1);
    snapshot.candidateCount=0;body::put32(0x10001100,0x10002000);assert(!nativeShapes::inventory(snapshot,world));body::put32(0x10001100,0x10002100);
    snapshot.candidateCount=0;putWord(world,0x24,0x10002000);body::put32(0x10002010,0);
    assert(nativeShapes::inventory(snapshot,world)&&snapshot.emptyFixed==1&&snapshot.candidateCount==2);
    putWord(world,0x24,0);body::put32(0x10002010,0x10003000);
    nativeShapes::Block rotated;assert(nativeShapes::readBlock(rotated,0x10004000,64));
    putFloat(rotated,0,0);putFloat(rotated,8,-1);putFloat(rotated,32,1);putFloat(rotated,40,0);putFloat(rotated,48,10);
    assert(nativeShapes::rigidTransform(rotated));
    float worldLo[]{10,0,-2},worldHi[]{14,3,0};auto local=nativeShapes::localQuery(rotated,worldLo,worldHi);
    assert(local.lo[0]==0&&local.hi[0]==2&&local.lo[2]==0&&local.hi[2]==4);
    putFloat(rotated,0,2);assert(!nativeShapes::rigidTransform(rotated));putFloat(rotated,0,0);
    snapshot.candidates[0].transform=rotated;snapshot.candidates[0].body=1;snapshot.candidates[1]=snapshot.candidates[0];snapshot.candidates[1].body=2;
    snapshot.keyCount=1;snapshot.triangleCount=0;nativeShapes::currentKeys[0]=42;nativeShapes::harvestFrame=outer.gpr[1];
    body::put32(0x12000,0x1040677c);guest[0x12008]=2;
    body::putf(0x12020,1);body::putf(0x12034,1);body::putf(0x12048,1);
    auto readLeaf=[&](unsigned root){nativeShapes::currentRoot=root;outer.gpr[3]=0x12000;outer.gpr[29]=0;nativeShapes::leaf(&outer);};
    readLeaf(0);readLeaf(0);readLeaf(1);
    assert(snapshot.triangleCount==2&&snapshot.triangles[0].v[0]==10&&snapshot.triangles[0].v[2]==-1&&snapshot.triangles[0].v[6]==11);
    // Owned 256-key codecave buffer: canaries armed per query, full buffer
    // subdivides, an overrun canary or foreign buffer is never consumed.
    {
        constexpr uint32_t buffer=0x01800000;assert(VirtualAlloc(guest+buffer,0x10000,MEM_COMMIT,PAGE_READWRITE));
        auto query=[&](uint32_t count,uint32_t address){
            QueryPerformanceCounter(&now);snapshot.qpc=now.QuadPart;snapshot.queries=0;snapshot.budgetExceeded=false;
            nativeShapes::rootCursor=snapshot.candidateCount;nativeShapes::queryPending=1;
            nativeShapes::queryStack[0]={{0,0,0},{8,2,4},0};
            outer.gpr[4]=buffer;nativeShapes::nextQuery(&outer);assert(nativeShapes::queryPending==0&&snapshot.queries==1);
            for(uint32_t i=0;i<count&&i<256;i++)body::put32(buffer+i*4,1000+i);
            outer.gpr[3]=count;outer.gpr[4]=address;nativeShapes::keys(&outer);
        };
        snapshot.compoundComplete=true;snapshot.keyFaults=snapshot.saturatedQueries=0;
        query(200,buffer);assert(outer.gpr[3]==200&&snapshot.keyCount==200&&nativeShapes::currentKeys[199]==1199&&snapshot.compoundComplete&&!snapshot.keyFaults);
        query(256,buffer);assert(outer.gpr[3]==0&&snapshot.saturatedQueries==1&&nativeShapes::queryPending==2);
        assert(nativeShapes::queryStack[0].lo[0]==4&&nativeShapes::queryStack[1].hi[0]==4&&nativeShapes::queryStack[1].depth==1);
        assert(snapshot.compoundComplete);
        QueryPerformanceCounter(&now);snapshot.qpc=now.QuadPart;nativeShapes::rootCursor=snapshot.candidateCount;
        outer.gpr[4]=buffer;nativeShapes::nextQuery(&outer);body::put32(buffer+256*4,0); // native overran capacity
        outer.gpr[3]=3;outer.gpr[4]=buffer;nativeShapes::keys(&outer);
        assert(outer.gpr[3]==0&&snapshot.keyFaults==1&&!snapshot.compoundComplete);
        snapshot.compoundComplete=true;query(3,buffer+4);assert(outer.gpr[3]==0&&snapshot.keyFaults==2&&!snapshot.compoundComplete);
        snapshot.compoundComplete=true;query(257,buffer);assert(outer.gpr[3]==0&&snapshot.keyFaults==2&&snapshot.overCapacity==1&&snapshot.saturatedQueries==2&&snapshot.compoundComplete);
        nativeShapes::queryPending=0;
        snapshot.compoundComplete=true;outer.gpr[4]=0x10000000;nativeShapes::queryPending=1;
        nativeShapes::nextQuery(&outer);assert(outer.gpr[3]==0&&snapshot.keyFaults==3&&!snapshot.compoundComplete); // not codecave
        nativeShapes::queryPending=0;
    }
    // Resumable read: a whole-region harvest that meets the budget inside a
    // signed root keeps its unanswered queries; only the request naming the
    // same root, signature and next chunk resumes them.
    {
        constexpr uint32_t buffer=0x01800000;auto& h=nativeShapes::pending;
        auto savedCandidate=h.candidates[0];auto savedCount=h.candidateCount;
        nativeRegions::Request r;r.key={1,2,3};r.stamp.generation=7;h.region=r;
        float lo[]{10,0,-2},hi[]{14,3,0};std::copy_n(lo,3,h.queryLo);std::copy_n(hi,3,h.queryHi);
        h.candidateCount=1;auto& c=h.candidates[0];c={};c.body=0x10004000;c.shape=0x10005000;c.vtable=0x1040D2E8;
        c.kind=nativeShapes::Kind::Mesh;c.overlap=true;c.shapeBytes.size=0x100;c.transform=rotated;
        LARGE_INTEGER second;QueryPerformanceFrequency(&second);
        auto begin=[&]{
            QueryPerformanceCounter(&now);h.qpc=now.QuadPart;h.queries=0;h.budgetExceeded=h.continued=false;h.compoundComplete=true;
            h.unknownLeaves=h.keyFaults=h.triangleCount=h.doneRoots=h.reusedRoots=h.queriedRoots=0;
            nativeShapes::rootCursor=0;nativeShapes::queryPending=0;nativeShapes::rootOpen=false;
            c.done=c.partial=c.reused=false;c.chunk=0;c.signature=0;outer.gpr[4]=buffer;nativeShapes::nextQuery(&outer);
        };
        auto answer=[&](uint32_t count){for(uint32_t i=0;i<count&&i<256;i++)body::put32(buffer+i*4,2000+i);outer.gpr[3]=count;outer.gpr[4]=buffer;nativeShapes::keys(&outer);outer.gpr[4]=buffer;};
        begin();assert(outer.gpr[3]==c.shape&&h.queries==1&&c.signature&&nativeShapes::rootOpen&&!c.chunk);
        answer(256);assert(nativeShapes::queryPending==2);                 // saturated: two halves left
        h.qpc-=second.QuadPart;nativeShapes::nextQuery(&outer);            // the budget ends the harvest here
        auto& kept=nativeShapes::continuation;
        assert(outer.gpr[3]==0&&c.partial&&!c.done&&h.continued&&h.budgetExceeded&&!h.compoundComplete&&kept.count==2&&kept.chunk==1&&kept.body==c.body&&kept.signature==c.signature);
        auto signature=c.signature;
        // A request that does not name the read starts the root again.
        begin();assert(outer.gpr[3]==c.shape&&!c.chunk&&nativeShapes::queryPending==0&&kept.count==2);
        // Wrong chunk, signature, region or cache generation: start again.
        r.resume={c.body,signature};r.resumeChunk=2;h.region=r;begin();assert(!c.chunk&&nativeShapes::queryPending==0);
        r.resumeChunk=1;r.resume.signature=signature+1;h.region=r;begin();assert(!c.chunk);
        r.resume.signature=signature;r.key={1,2,4};h.region=r;begin();assert(!c.chunk);
        r.key={1,2,3};r.stamp.generation=8;h.region=r;begin();assert(!c.chunk);
        r.part=0;r.stamp.generation=7;h.region=r;begin();assert(!c.chunk&&kept.count==2); // parts never resume
        // The named read resumes with both unanswered queries, as chunk 1, and finishes.
        r.part=nativeRegions::kWhole;h.region=r;begin();
        assert(outer.gpr[3]==c.shape&&c.chunk==1&&nativeShapes::queryPending==1&&!kept.count&&h.queries==1);
        answer(3);nativeShapes::nextQuery(&outer);assert(outer.gpr[3]==c.shape&&nativeShapes::queryPending==0&&!c.done);
        answer(3);nativeShapes::nextQuery(&outer);
        assert(outer.gpr[3]==0&&c.done&&!c.partial&&c.chunk==1&&h.doneRoots==1&&!h.continued&&!h.budgetExceeded&&h.compoundComplete);
        // A fault before the budget is a failure, not a resumable stop.
        r.resume={};h.region=r;begin();answer(256);h.unknownLeaves=1;h.qpc-=second.QuadPart;nativeShapes::nextQuery(&outer);
        assert(!c.partial&&!h.continued&&h.budgetExceeded&&!kept.count);
        // A part request that meets the budget is over budget as before.
        r.part=3;h.region=r;begin();answer(256);h.qpc-=second.QuadPart;nativeShapes::nextQuery(&outer);assert(!c.partial&&!h.continued&&h.budgetExceeded);
        h.candidates[0]=savedCandidate;h.candidateCount=savedCount;h.region.reset();kept={};
        nativeShapes::queryPending=0;nativeShapes::rootOpen=false;h.unknownLeaves=0;h.compoundComplete=true;h.budgetExceeded=false;
    }
    // Decoded roots: a compressed-mesh root is not queried at all; a static
    // compound is queried, but the keys of its mesh instances are dropped
    // before the guest's leaf loop, leaving only the other children.
    {
        constexpr uint32_t buffer=0x01800000,instances=0x10009000,meshChild=0x1000A000,hullChild=0x1000A100;auto& h=nativeShapes::pending;
        auto savedCandidate=h.candidates[0];auto savedCount=h.candidateCount;
        nativeRegions::Request r;r.key={1,2,3};h.region=r;float lo[]{10,0,-2},hi[]{14,3,0};std::copy_n(lo,3,h.queryLo);std::copy_n(hi,3,h.queryHi);
        h.candidateCount=1;auto& c=h.candidates[0];c={};c.body=0x10004000;c.shape=0x10005000;c.vtable=0x1040DA74;c.kind=nativeShapes::Kind::Compound;c.overlap=true;
        c.shapeBytes.size=0x100;c.transform=rotated;c.shapeBytes.bytes[0x18]=15;putWord(c.shapeBytes,0x1C,0x7FFF);putWord(c.shapeBytes,0x20,instances);putWord(c.shapeBytes,0x24,2);putWord(c.shapeBytes,0x28,0x80000002);
        memset(guest+instances,0,0x80);body::put32(instances+0x30,meshChild);body::put32(instances+0x70,hullChild);body::put32(meshChild,0x1040D2E8);body::put32(hullChild,0x10401B14);
        auto begin=[&]{
            QueryPerformanceCounter(&now);h.qpc=now.QuadPart;h.queries=0;h.budgetExceeded=h.continued=false;h.compoundComplete=true;
            h.unknownLeaves=h.keyFaults=h.triangleCount=h.doneRoots=h.reusedRoots=h.queriedRoots=h.decodedRoots=h.droppedKeys=0;
            nativeShapes::rootCursor=0;nativeShapes::queryPending=0;nativeShapes::rootOpen=false;
            c.done=c.partial=c.reused=c.decoded=false;c.chunk=0;c.signature=0;outer.gpr[4]=buffer;nativeShapes::nextQuery(&outer);
        };
        begin();assert(outer.gpr[3]==c.shape&&!c.decoded&&c.signature);
        const uint32_t keys[]{0u<<15|5,1u<<15|9,0u<<15|6,1u<<15|2};
        auto answer=[&]{for(int i=0;i<4;i++)body::put32(buffer+i*4,keys[i]);outer.gpr[3]=4;outer.gpr[4]=buffer;nativeShapes::keys(&outer);};
        answer();assert(outer.gpr[3]==4&&h.keyCount==4&&!h.droppedKeys);                         // not decoded: every key is fetched
        r.decodedCount=1;r.decoded[0]={c.body,c.signature};h.region=r;
        begin();assert(outer.gpr[3]==c.shape&&c.decoded&&h.decodedRoots==1);answer();
        assert(outer.gpr[3]==2&&h.keyCount==2&&h.droppedKeys==2&&body::u32(buffer)==keys[1]&&body::u32(buffer+4)==keys[3]&&nativeShapes::currentKeys[1]==keys[3]&&h.compoundComplete);
        outer.gpr[4]=buffer;nativeShapes::nextQuery(&outer);assert(outer.gpr[3]==0&&c.done);
        r.decoded[0].signature+=1;h.region=r;begin();assert(!c.decoded);                         // another signature: read natively
        r.decoded[0].signature-=1;h.region=r;begin();body::put32(buffer,7u<<15);outer.gpr[3]=1;outer.gpr[4]=buffer;nativeShapes::keys(&outer);
        assert(outer.gpr[3]==0&&h.keyFaults==1&&!h.compoundComplete);                             // instance beyond the array
        c.vtable=0x1040D2E8;c.kind=nativeShapes::Kind::Mesh;begin();                              // (signature differs with the vtable)
        r.decoded[0]={c.body,c.signature};h.region=r;begin();
        assert(outer.gpr[3]==0&&c.decoded&&c.done&&h.decodedRoots==1&&!h.queries&&!h.queriedRoots); // mesh root: nothing queried
        h.candidates[0]=savedCandidate;h.candidateCount=savedCount;h.region.reset();
        nativeShapes::queryPending=0;nativeShapes::rootOpen=false;h.keyFaults=0;h.compoundComplete=true;h.budgetExceeded=false;
    }
    // SkyCraft collision policy: layer filter (session-09 observed filter words),
    // guarded virtual getAabb, tree/heightfield/fallback classification.
    {
        using nativeShapes::includedLayer;using nativeShapes::Kind;
        assert(includedLayer(0x3c1000c8)&&includedLayer(0x3c1000c9)&&includedLayer(0x3c1000ca)&&includedLayer(0x3c1000cc)); // ground x3, tree
        assert(includedLayer(0x3c000008)&&!includedLayer(0x3c000006));          // ground mesh tile; water heightfield
        assert(!includedLayer(0x420003)&&!includedLayer(0x3c44000e)&&!includedLayer(0x3c670001)); // player, hit-only-water, small object
        assert(!includedLayer(4)&&!includedLayer(13)&&!includedLayer(5)&&!includedLayer(0x40000008)); // NPC, NPC-no-hit, ragdoll layer/bit
        assert(includedLayer(0x80000000u|8u<<25)&&!includedLayer(0x80000000u|3u<<25)); // ground-hit mask form
        assert(VirtualAlloc(guest+0x1040D000,0x2000,MEM_COMMIT,PAGE_READWRITE));
        constexpr uint32_t vt=0x10008000;body::put32(vt+0x30,0);body::put32(vt+0x34,0x04024BB8);
        assert(nativeShapes::virtualAabb(vt)==0x04024BB8);
        body::put32(vt+0x30,4);assert(!nativeShapes::virtualAabb(vt));body::put32(vt+0x30,0);      // this-adjustment
        body::put32(vt+0x34,0x01800000);assert(!nativeShapes::virtualAabb(vt));                    // outside .text
        body::put32(vt+0x34,0x04024BBA);assert(!nativeShapes::virtualAabb(vt));                    // misaligned
        body::put32(0x1040DA74+0x34,0x041336EC);assert(nativeShapes::virtualAabb(0x1040DA74)==0x041336EC);
        body::put32(0x1040DA74+0x34,0x04024BB8);assert(!nativeShapes::virtualAabb(0x1040DA74));    // pinned mismatch
        assert(!nativeShapes::virtualAabb(0x0FFFFFF0)&&!nativeShapes::virtualAabb(0x10462BA0));
        static nativeShapes::Snapshot policy;auto candidate=[](uint32_t vtable,uint8_t type,float size){
            nativeShapes::Candidate c;c.vtable=vtable;c.shapeBytes.bytes[8]=type;c.included=true;
            for(int k=0;k<3;k++){c.lo[k]=0;c.hi[k]=size;}return c;};
        auto kind=[&](nativeShapes::Candidate c){nativeShapes::classify(policy,c);return c.kind;};
        assert(kind(candidate(0x1040DA74,16,500))==Kind::Compound&&kind(candidate(0x1040D2E8,17,250))==Kind::Mesh);
        assert(kind(candidate(0x10322224,12,9000))==Kind::Height);
        assert(kind(candidate(0x104033A8,5,2))==Kind::Fallback&&kind(candidate(0x10403108,10,2))==Kind::Fallback&&kind(candidate(0x10405EA8,0,2))==Kind::Fallback);
        assert(kind(candidate(0x10402F18,11,64*1.3f))==Kind::Fallback&&kind(candidate(0x10402F18,11,64*1.3f+1))==Kind::Unsupported); // SkyCraft 64-block limit
        assert(kind(candidate(0x10400000,28,2))==Kind::Unsupported&&policy.unsupported==2&&policy.fallbacks==4);
        assert(nativeShapes::treeCalls(0x1040D2E8).query==0x040F3EC8&&nativeShapes::treeCalls(0x1040D2E8).child==0x040EF924&&!nativeShapes::treeCalls(0x104033A8).query);
        // Bounds pass: excluded player body is skipped (no getAabb call, not
        // unsupported); included bodies dispatch through their own vtable.
        auto& h=nativeShapes::pending;h.candidateCount=3;h.excluded=h.unboundable=h.unsupported=h.fallbacks=h.boundsRead=0;
        h.candidates[0]=candidate(0x104033A8,5,1);h.candidates[0].included=nativeShapes::includedLayer(0x420003);
        h.candidates[1]=candidate(vt,10,1);h.candidates[1].shape=0x10003000;
        h.candidates[2]=candidate(0x10009000,3,1);   // unreadable vtable contents -> unavailable
        body::put32(vt+0x34,0x04024BB8);
        for(int k=0;k<3;k++){h.queryLo[k]=0;h.queryHi[k]=10;}
        QueryPerformanceCounter(&now);h.qpc=now.QuadPart;h.budgetExceeded=false;
        nativeShapes::harvestFrame=outer.gpr[1];nativeShapes::boundsCursor=0;
        nativeShapes::boundsNext(&outer);
        assert(h.excluded==1&&nativeShapes::boundsCursor==1&&outer.gpr[12]==0x04024BB8&&outer.gpr[3]==0x10003000);
        for(int k=0;k<3;k++){body::putf(outer.gpr[1]+0x540+k*4,1+k);body::putf(outer.gpr[1]+0x550+k*4,2+k);}
        nativeShapes::boundsResult(&outer);
        assert(h.candidates[1].overlap&&h.candidates[1].kind==Kind::Fallback&&h.fallbacks==1);
        body::put32(0x10009000+0x34,0x7FFFFFF0);nativeShapes::boundsNext(&outer);
        assert(h.unboundable==1&&nativeShapes::boundsCursor==3&&outer.gpr[3]==0);
        // Non-overlap is read but not classified.
        h.candidateCount=4;h.candidates[3]=candidate(vt,10,1);nativeShapes::boundsNext(&outer);
        for(int k=0;k<3;k++){body::putf(outer.gpr[1]+0x540+k*4,20);body::putf(outer.gpr[1]+0x550+k*4,21);}
        nativeShapes::boundsResult(&outer);assert(!h.candidates[3].overlap&&h.candidates[3].kind==Kind::Excluded&&h.fallbacks==1);
        nativeShapes::harvestFrame=0;
        // Fallback box: 12 outward triangles, exact native AABB in MC space.
        float boxLo[]{-1027.16809f,251.188492f,1812.87195f},boxHi[]{-1027.16809f+1.3f,251.188492f+2.6f,1812.87195f+3.9f};
        std::vector<botwcraft::proto::ColTri> box;nativeShapes::boxTriangles(boxLo,boxHi,box);assert(box.size()==12);
        for(const auto& t:box){
            float a[3],b[3],n[3],centre[3];nativeGeometry::sub(t.v+3,t.v,a);nativeGeometry::sub(t.v+6,t.v,b);nativeGeometry::cross(a,b,n);
            for(int k=0;k<3;k++){centre[k]=(t.v[k]+t.v[k+3]+t.v[k+6])/3-float(k==0?.5:k==1?101:1.5);
                for(int j=0;j<3;j++)assert(std::abs(t.v[j*3+k]-float(k==1?100:0)-(k==0?.5f:k==1?1.f:1.5f))<=float(k+1)*.5f+1e-4f);}
            assert(nativeGeometry::dot(n,centre)>0&&nativeGeometry::valid(t));
        }
        // Region completeness requires every gate.
        h.region=nativeRegions::Request{};h.inventoryComplete=h.compoundComplete=true;h.budgetExceeded=false;
        h.unknownLeaves=h.keyFaults=h.unsupported=h.unboundable=h.heightfields=0;h.triangleCount=0;
        assert(nativeShapes::regionComplete(h));
        h.unboundable=1;assert(!nativeShapes::regionComplete(h));h.unboundable=0;
        h.unsupported=1;assert(!nativeShapes::regionComplete(h));h.unsupported=0;
        h.heightfields=1;h.terrain.complete=false;assert(!nativeShapes::regionComplete(h));h.terrain.complete=true;assert(nativeShapes::regionComplete(h));h.heightfields=0;
        h.budgetExceeded=true;assert(!nativeShapes::regionComplete(h));h.budgetExceeded=false;
        h.triangleCount=h.triangles.size();assert(!nativeShapes::regionComplete(h));h.triangleCount=0;
        // Loaded-terrain guard: a 250-unit ground tile must cover all corners.
        h.candidateCount=1;h.candidates[0]=candidate(0x1040D2E8,17,250);h.candidates[0].filter=0x3c000008;h.candidates[0].bounded=true;
        for(int k=0;k<3;k++){h.queryLo[k]=10;h.queryHi[k]=21.7f;}
        assert(nativeShapes::terrainLoaded(h)&&nativeShapes::regionComplete(h));
        h.queryHi[0]=251;assert(!nativeShapes::terrainLoaded(h)&&!nativeShapes::regionComplete(h)); // neighbour tile not loaded
        h.candidateCount=2;h.candidates[1]=h.candidates[0];h.candidates[1].lo[0]=250;h.candidates[1].hi[0]=500;assert(nativeShapes::terrainLoaded(h));
        h.candidates[1].filter=0x3c000006;assert(!nativeShapes::terrainLoaded(h));                // water is not ground
        h.candidateCount=1;h.candidates[0]=candidate(0x1040DA74,16,20);assert(nativeShapes::terrainLoaded(h)); // no tiles: shrine/interior
        h.candidateCount=0;
        h.region.reset();assert(!nativeShapes::regionComplete(h));
        // Bv-tree child that is not a triangle: an eligible convex child gets
        // its own getAabb (SkyCraft per-child fallback); others stay unknown.
        nativeShapes::harvestFrame=outer.gpr[1];nativeShapes::currentRoot=1;h.candidates[1].body=0x55;
        h.keyCount=3;nativeShapes::currentKeys[0]=7;nativeShapes::currentKeys[1]=8;nativeShapes::currentKeys[2]=9;
        h.leafBoxCount=h.unknownCount=h.unknownLeaves=0;h.compoundComplete=true;nativeShapes::leafSeen.fill(0);
        body::put32(0x12100,vt);guest[0x12108]=5;body::put32(vt+0x34,0x04024BB8);
        outer.gpr[3]=0x12100;outer.gpr[29]=0;nativeShapes::leaf(&outer);
        assert(outer.gpr[3]==0x12100&&outer.gpr[12]==0x04024BB8&&outer.gpr[4]==outer.gpr[1]+0x500&&!h.unknownLeaves);
        for(int k=0;k<3;k++){body::putf(outer.gpr[1]+0x540+k*4,-1027.f+k);body::putf(outer.gpr[1]+0x550+k*4,-1026.f+k);}
        nativeShapes::leafBounds(&outer);assert(h.leafBoxCount==1&&h.leafBoxes[0].key==7&&h.leafBoxes[0].body==0x55&&h.compoundComplete);
        outer.gpr[3]=0x12100;outer.gpr[29]=1;nativeShapes::leaf(&outer);
        body::putf(outer.gpr[1]+0x550,-1027.f+100);nativeShapes::leafBounds(&outer); // beyond 64 MC blocks
        assert(h.leafBoxCount==1&&h.unknownLeaves==1&&h.unknown[0].reason==5&&!h.compoundComplete);
        guest[0x12108]=28;outer.gpr[3]=0x12100;outer.gpr[29]=2;nativeShapes::leaf(&outer);
        assert(outer.gpr[3]==0&&h.unknownLeaves==2&&h.unknown[1].reason==2&&h.unknown[1].type==28);
        outer.gpr[3]=0x12100;outer.gpr[29]=0;nativeShapes::leaf(&outer);assert(outer.gpr[3]==0&&h.unknownLeaves==2); // duplicate key
        nativeShapes::harvestFrame=0;
    }
    nativeShapes::harvestFrame=0;
    putWord(shape,0,0x10322224);putWord(shape,0x28,100);putWord(shape,0x2c,100);putWord(shape,0x80,0x10000200);
    for(int k=0;k<3;k++){
        putFloat(shape,0x40+k*4,k==1?1.f:.5f);putFloat(shape,0x94+k*4,k==1?1000.f:10.f);
        putFloat(shape,0xa0+k*4,k==1?0.f:10.f);putFloat(transform,k*20,1);
        terrainSnapshot.queryLo[k]=k==1?99:1.1f;terrainSnapshot.queryHi[k]=k==1?101:1.9f;
    }
    putFloat(shape,0xb8,.5f);
    nativeShapes::terrainCandidate(terrainSnapshot,shape,transform);
    auto& grid=terrainSnapshot.terrain;assert(grid.shape&&grid.x==2&&grid.z==2&&grid.width==3&&grid.depth==3&&grid.count==9);
    std::vector<ColTri> terrainTris;nativeShapes::terrainTriangles(grid,terrainTris);assert(terrainTris.empty());
    grid.complete=true;for(int i=0;i<9;i++)grid.height[i]=100+float(i*i)/10;
    nativeShapes::terrainTriangles(grid,terrainTris);assert(terrainTris.size()==8);
    for(const auto& t:terrainTris){float a[3],b[3],n[3];nativeGeometry::sub(t.v+3,t.v,a);nativeGeometry::sub(t.v+6,t.v,b);nativeGeometry::cross(a,b,n);assert(n[1]>0&&t.flags==kTriTerrain);}
    grid={};putWord(shape,0,0x1032214C);putFloat(shape,0xac,1.5f);putFloat(shape,0xb0,1.5f);putFloat(shape,0xb4,.1f);
    nativeShapes::terrainCandidate(terrainSnapshot,shape,transform);assert(!grid.shape);
    putFloat(shape,0xb4,0);putFloat(transform,0,0);
    nativeShapes::terrainCandidate(terrainSnapshot,shape,transform);assert(!grid.shape);
    // Exercise the production protocol11 ring: clear, precision, occupancy,
    // consumed-head readiness; age, failed refresh and eviction retain data.
    live::base=static_cast<uint8_t*>(VirtualAlloc(nullptr,kOffCollisionRing+kCollisionRingBytes,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    assert(live::base);live::connected=true;live::failed=false;
    nativeStream::reset(4,8,0x1234,"check");p[0]=1;p[2]=1;
    auto& stream=nativeRegions::cache;a=stream.next(p,v,3000);assert(a);
    assert(stream.finish(*a,true,{floor},3001));nativeStream::publish(3002);
    auto ring=live::base+kOffCollisionRing;uint64_t head=*reinterpret_cast<uint64_t*>(ring),offset=0;
    for(uint32_t type:{uint32_t(kColClear),uint32_t(kColTris),uint32_t(kColRegion)}){
        ColMsgHeader header;memcpy(&header,ring+0x80+offset,8);assert(header.type==type);
        uint32_t ep;memcpy(&ep,ring+0x88+offset+(type==kColClear?0:24),4);assert(ep==walking::epoch);
        offset+=(8ull+header.payloadBytes+7)&~7ull;
    }
    assert(offset==head&&stream.entries.at(a->key).publishedHead==head);
    assert(!stream.covers(lo,hi,3003,head-1));assert(stream.covers(lo,hi,3003,head));
    *reinterpret_cast<uint64_t*>(ring+0x40)=head;
    // Formerly publish(3751) wiped every region here. Now: no clear, the region
    // stays covered within kMaxAgeMs, an unchanged refresh is not re-sent, an
    // incomplete refresh keeps the published data, and only age pauses it.
    uint32_t priorEpoch=walking::epoch;nativeStream::publish(3751);
    assert(walking::epoch==priorEpoch&&stream.entries.size()==1&&*reinterpret_cast<uint64_t*>(ring)==head);
    assert(stream.covers(lo,hi,3751,head));
    a=turn(stream,p,v,3760,home,false);
    assert(stream.finish(*a,true,{floor},3761)&&stream.unchanged==1);nativeStream::publish(3762);
    assert(*reinterpret_cast<uint64_t*>(ring)==head&&stream.covers(lo,hi,3762+2900,head));
    a=turn(stream,p,v,4600,home,false);assert(a&&stream.finish(*a,false,{},4601)&&stream.retained==1);
    nativeStream::publish(4602);assert(*reinterpret_cast<uint64_t*>(ring)==head&&stream.entries.at(a->key).complete);
    assert(stream.covers(lo,hi,4602,head)&&!stream.covers(lo,hi,3761+nativeRegions::kMaxAgeMs,head));
    // Changed geometry is republished in place; the older consumed publication
    // keeps readiness until Minecraft consumes the replacement.
    ColTri raised=floor;for(int k=1;k<9;k+=3)raised.v[k]+=.5f;
    a=turn(stream,p,v,5400,home,false);assert(a&&stream.finish(*a,true,{raised},5401));nativeStream::publish(5402);
    uint64_t replaced=*reinterpret_cast<uint64_t*>(ring);ColMsgHeader next;memcpy(&next,ring+0x80+head,8);
    assert(replaced>head&&next.type==kColTris&&stream.covers(lo,hi,5402,head));
    // Ground check: first exported surface from above (raised floor wins).
    {float n[3];double y=nativeStream::exportedGround(1,1,110,90,n);assert(std::abs(y-100.75)<1e-5&&n[1]>.99f);
     assert(std::isnan(nativeStream::exportedGround(1,1,100,90,n))&&std::isnan(nativeStream::exportedGround(20,1,110,90,n)));}
    // A genuine lifecycle change is the only clear.
    nativeStream::reset(5,8,0x1234,"check-epoch");nativeStream::publish(5403);
    memcpy(&next,ring+0x80+replaced,8);assert(next.type==kColClear&&walking::epoch==priorEpoch+1&&stream.entries.empty());
    // Ground-hit masks follow BOTW's filter: the shrine-cave animal blocker
    // (ignores every GroundHit but Animal) does not collide with Link.
    assert(!nativeShapes::includedLayer(0x90FFFD00u)&&nativeShapes::includedLayer(0x90FFFD00u&~0x100u)&&nativeShapes::includedLayer(0x3C1000C8u));
    // Oriented box: route-07's rotated hkpBoxShape exports 12 outward
    // triangles inside its AABB instead of the AABB itself.
    {
        nativeShapes::Candidate box;box.vtable=0x10400CA8;box.aabbFn=0x03FFFBFC;box.shapeBytes.size=0x100;box.shapeBytes.bytes[8]=3;box.transform.size=64;
        auto put=[](nativeShapes::Block& b,uint32_t o,float f){uint32_t v;memcpy(&v,&f,4);v=__builtin_bswap32(v);memcpy(b.bytes.data()+o,&v,4);};
        put(box.shapeBytes,0x10,0);put(box.shapeBytes,0x20,14.9869f);put(box.shapeBytes,0x24,15);put(box.shapeBytes,0x28,9.9866f); // solved from the native AABB
        float r[16]{-.777572751f,0,-.628792524f,0, 0,1,0,0, .628792524f,0,-.777572751f,0, -1074,248.270203f,1852.19995f,0};
        for(int j=0;j<16;j++)put(box.transform,j*4,r[j]);
        std::vector<ColTri> tris;assert(nativeShapes::orientedBox(box,tris)&&tris.size()==12);
        double centre[]{(-1074-nativeRegions::anchor[0])/1.3,(248.270203-nativeRegions::anchor[1])/1.3+100,(1852.19995-nativeRegions::anchor[2])/1.3};
        double lo[]{1e9,1e9,1e9},hi[]{-1e9,-1e9,-1e9};
        for(const auto& t:tris){
            float a[3],b[3],n[3],d[3],c[3];nativeGeometry::sub(t.v+3,t.v,a);nativeGeometry::sub(t.v+6,t.v,b);nativeGeometry::cross(a,b,n);
            for(int k=0;k<3;k++){c[k]=(t.v[k]+t.v[k+3]+t.v[k+6])/3;d[k]=c[k]-float(centre[k]);}
            assert(nativeGeometry::dot(n,d)>0);
            for(int k=0;k<9;k++){lo[k%3]=std::min(lo[k%3],double(t.v[k]));hi[k%3]=std::max(hi[k%3],double(t.v[k]));}
        }
        // Same extent as the native AABB (-1091.93..-1056.07 x, 1835.01..1869.39 z);
        // half extents solved from it to 4 digits, hence the 0.01 MC tolerance.
        assert(std::abs(lo[0]-(-1091.93115-nativeRegions::anchor[0])/1.3)<1e-2&&std::abs(hi[2]-(1869.38721-nativeRegions::anchor[2])/1.3)<1e-2);
        // A point inside the AABB corner but outside the rotated box is free.
        double x=(-1090-nativeRegions::anchor[0])/1.3,z=(1868-nativeRegions::anchor[2])/1.3,y=centre[1];
        bool inside=true;for(const auto& t:tris){float a[3],b[3],n[3];nativeGeometry::sub(t.v+3,t.v,a);nativeGeometry::sub(t.v+6,t.v,b);nativeGeometry::cross(a,b,n);
            double d=(x-t.v[0])*n[0]+(y-t.v[1])*n[1]+(z-t.v[2])*n[2];inside&=d<=0;}
        assert(!inside);
        box.vtable=0x1040FFFF;tris.clear();assert(!nativeShapes::orientedBox(box,tris)); // unpinned: AABB fallback
    }
    // Split acquisition: an over-budget region retries as eight octants and
    // completes once, deduplicated, keeping the old data meanwhile.
    {
        nativeRegions::Cache split;split.reset(9,9,1);double at[]{1,100.25,1},still[]{0,0,0};
        auto job=turn(split,at,still,100,home,false);assert(job&&job->part==nativeRegions::kWhole);
        assert(split.finish(*job,false,{},101,true)&&split.splits==1&&split.entries.at(job->key).split);
        ColTri across{{3,100.25f,3,5,100.25f,3,5,100.25f,5},0}; // spans four octants
        for(int part=0;part<8;part++){
            auto p=turn(split,at,still,102+part,home,false);assert(p&&p->key==job->key&&p->part==part);
            std::vector<ColTri> tris;if(nativeGeometry::overlaps(across,p->lo,p->hi))tris.push_back(across);
            assert(split.finish(*p,true,tris,103+part));
            assert(split.entries.at(job->key).complete==(part==7));
        }
        assert(split.entries.at(job->key).triangles.size()==1&&split.completed==1);
        // A failed octant restarts the set; the region stays split.
        auto p=turn(split,at,still,2000,home,false);assert(p&&p->part==0);assert(split.finish(*p,false,{},2001));
        assert(split.entries.at(job->key).complete&&split.entries.at(job->key).done.none());
        // An octant that overruns alone is replaced by its 8 sub-octants (2 MC).
        p=turn(split,at,still,2800,home,false);assert(p&&p->part==0);assert(split.finish(*p,false,{},2801,true));
        auto& e=split.entries.at(job->key);assert(e.deep==1&&e.partsActive);
        int parts=0;uint64_t t=2802;
        for(;;){
            auto q=turn(split,at,still,t,home,false);++parts;
            if(q->part>=8)assert(q->hi[0]-q->lo[0]==3.f);
            std::vector<ColTri> tris;if(nativeGeometry::overlaps(across,q->lo,q->hi))tris.push_back(across);
            assert(split.finish(*q,true,tris,t+1));t+=2;
            if(!e.partsActive)break;
        }
        assert(parts==15&&e.complete&&e.triangles.size()==1&&e.cycleMs>0); // 8 sub + 7 octants
    }
    // Scheduling: the regions next to the player's (and the travel point's)
    // region are answered before any other, missing or oldest first, so a
    // moving player does not meet a neighbour that was left unread or aged out.
    {
        nativeRegions::Cache order;order.reset(15,15,1);double at[]{4,100,4},still[]{0,0,0},fast[]{10,0,0};std::set<nativeRegions::Key> seen;
        auto distance=[&](const nativeRegions::Key& k){int d=0;for(int i=0;i<3;i++)d=std::max(d,std::abs(k[i]-home[i]));return d;};
        for(int i=0;i<27;i++){auto j=order.next(at,still,100+i);assert(j&&distance(j->key)<=1&&seen.insert(j->key).second&&order.finish(*j,true,{},100+i));}
        auto j=order.next(at,still,300);assert(j&&distance(j->key)==2&&order.finish(*j,true,{},300)); // neighbours not due: an outer region
        j=order.next(at,still,860);assert(j&&j->key==home&&order.finish(*j,true,{},860));              // the player's region is due every kRefreshMs
        j=order.next(at,fast,870);assert(j&&j->key[0]==2&&order.finish(*j,true,{},870));               // unread regions beside the travel point
        // Neighbours come round every kInnerMs, the one waiting longest first.
        for(uint64_t t=871;t<871+17;t++){j=order.next(at,fast,t);assert(j&&order.finish(*j,true,{},t));}
        j=order.next(at,still,101+nativeRegions::kInnerMs);assert(j&&distance(j->key)==1&&order.entries.at(j->key).attempted==101);
    }
    // Static reuse signature: fixed roots only; the reference count is ignored;
    // header, transform and static-compound instance bits all change it.
    {
        nativeShapes::Candidate c;c.body=0x10004000;c.shape=0x10005000;c.vtable=0x1040D2E8;c.filter=0x3C000008;
        c.shapeBytes.address=c.shape;c.shapeBytes.size=0x100;c.transform.size=64;
        for(int i=0;i<3;i++)putFloat(c.transform,i*20,1);
        auto mesh=nativeShapes::rootSignature(c);assert(mesh);
        c.shapeBytes.bytes[6]=9;assert(nativeShapes::rootSignature(c)==mesh);
        c.shapeBytes.bytes[0x20]^=1;assert(nativeShapes::rootSignature(c)!=mesh);c.shapeBytes.bytes[0x20]^=1;
        putFloat(c.transform,48,1);assert(nativeShapes::rootSignature(c)!=mesh);putFloat(c.transform,48,0);
        c.filter^=1;assert(nativeShapes::rootSignature(c)!=mesh);c.filter^=1;
        c.group=1;assert(!nativeShapes::rootSignature(c));c.group=0;assert(nativeShapes::rootSignature(c)==mesh);
        c.vtable=0x1040DA74;putWord(c.shapeBytes,0x20,0x10008000);putWord(c.shapeBytes,0x24,300);putWord(c.shapeBytes,0x28,0x8000012C);
        memset(guest+0x10008000,0,300*0x40);
        auto compound=nativeShapes::rootSignature(c);assert(compound&&compound!=mesh&&nativeShapes::rootSignature(c)==compound);
        guest[0x10008000+0x40*299+0x0F]^=0x10;assert(nativeShapes::rootSignature(c)!=compound); // last instance disabled
        guest[0x10008000+0x40*299+0x0F]^=0x10;assert(nativeShapes::rootSignature(c)==compound);
        guest[0x10008000+0x40*2+0x2F]^=1;assert(nativeShapes::rootSignature(c)!=compound);      // a disabled small key
        putWord(c.shapeBytes,0x24,301);assert(!nativeShapes::rootSignature(c));                  // count above capacity
        putWord(c.shapeBytes,0x24,9000);putWord(c.shapeBytes,0x28,9000);assert(!nativeShapes::rootSignature(c)); // unbounded
        putWord(c.shapeBytes,0x24,300);putWord(c.shapeBytes,0x20,0x10100000);assert(!nativeShapes::rootSignature(c)); // unreadable
    }
    // Static reuse in the cache: a fixed root is read once, then skipped while
    // its signature holds; full reads recur and audit what was held.
    {
        using nativeRegions::Roots;const uint32_t body=0x25208FC0;
        nativeRegions::Cache reuse;reuse.reset(11,11,1);double at[]{1,100.25,1},still[]{0,0,0};
        ColTri wall{{2,100,2,2,104,2,6,100,2},0},loose{{1,100.25f,1,1,100.25f,3,3,100.25f,1},0},moved{{2,100,3,2,104,3,6,100,3},0};
        auto job=turn(reuse,at,still,100,home,false);assert(job&&!job->knownCount);auto key=job->key;
        Roots read;read.fresh.push_back({body,77,{wall,wall}});
        assert(reuse.finish(*job,true,{loose},101,false,read));
        auto& e=reuse.entries.at(key);
        assert(e.complete&&e.triangles.size()==2&&e.statics.size()==1&&e.statics[0].triangles.size()==1&&reuse.fullReads==1&&e.statics[0].read==101);
        job=turn(reuse,at,still,900,home,false);assert(job&&job->key==key&&job->part==nativeRegions::kWhole&&job->knownCount==1&&job->known[0].body==body&&job->known[0].signature==77);
        Roots skipped;skipped.reused.push_back({body,77});
        assert(reuse.finish(*job,true,{loose},901,false,skipped)&&reuse.unchanged==1&&reuse.reuses==1&&e.updated==901&&e.statics[0].read==101);
        // A skipped root that is not held under that signature is unknown.
        job=turn(reuse,at,still,1700,home,false);skipped.reused[0].signature=78;
        assert(job->key==key&&reuse.finish(*job,true,{loose},1701,false,skipped)&&reuse.retained==1&&e.updated==901&&e.statics[0].signature==77);
        // Changed signature: the harvest re-read the root; it replaces the held one.
        job=turn(reuse,at,still,2500,home,false);Roots changed;changed.fresh.push_back({body,78,{moved}});
        assert(job->key==key&&reuse.finish(*job,true,{loose},2501,false,changed)&&e.statics[0].signature==78&&!reuse.staticStale);
        assert(e.triangles.size()==2&&!memcmp(&e.triangles[1],&moved,sizeof(moved))&&e.statics[0].read==2501);
        // A root that left the region is dropped with its triangles.
        job=turn(reuse,at,still,3300,home,false);assert(job->key==key&&job->knownCount==1&&reuse.finish(*job,true,{loose},3301));
        assert(e.statics.empty()&&e.triangles.size()==1);
        job=turn(reuse,at,still,4100,home,false);assert(job->key==key&&!job->knownCount&&reuse.finish(*job,true,{loose},4101,false,changed));
        // Audit: after kAuditMs the region is read in full again. Different
        // triangles under an unchanged signature are counted and replace the old.
        job=turn(reuse,at,still,4900,home,false);assert(job->key==key&&job->knownCount==1);skipped.reused[0].signature=78;assert(reuse.finish(*job,true,{loose},4901,false,skipped));
        job=turn(reuse,at,still,4101+nativeRegions::kAuditMs+8000,home,false);assert(job&&job->key==key&&!job->knownCount);
        Roots audit;audit.fresh.push_back({body,78,{moved}});
        assert(reuse.finish(*job,true,{loose},4102+nativeRegions::kAuditMs+8000,false,audit)&&!reuse.staticStale&&reuse.unchanged>=2);
        job=turn(reuse,at,still,4101+3*(nativeRegions::kAuditMs+8000),home,false);assert(job&&job->key==key&&!job->knownCount);
        audit.fresh[0].triangles={wall};
        assert(reuse.finish(*job,true,{loose},4102+3*(nativeRegions::kAuditMs+8000),false,audit)&&reuse.staticStale==1&&!memcmp(&e.triangles[1],&wall,sizeof(wall)));
        // Split region: full reads go by parts; refreshes in between are one
        // whole request that skips the held root.
        nativeRegions::Cache dense;dense.reset(12,12,1);
        job=turn(dense,at,still,100,home,false);assert(job&&dense.finish(*job,false,{},101,true));
        ColTri across{{3,100.25f,3,5,100.25f,3,5,100.25f,5},0};
        auto part=[&](const nativeRegions::Request& q,uint64_t signature){Roots r;r.fresh.push_back({body,signature,{}});
            if(nativeGeometry::overlaps(across,q.lo,q.hi))r.fresh[0].triangles.push_back(across);return r;};
        for(int i=0;i<8;i++){auto q=turn(dense,at,still,102+i,home,false);assert(q&&q->part==i&&!q->knownCount);assert(dense.finish(*q,true,{},103+i,false,part(*q,5)));}
        auto& d=dense.entries.at(key);
        assert(d.complete&&d.split&&d.triangles.size()==1&&d.statics.size()==1&&d.statics[0].triangles.size()==1&&d.partialRoots.empty());
        auto q=turn(dense,at,still,1000,home,false);assert(q&&q->key==key&&q->part==nativeRegions::kWhole&&q->knownCount==1);
        skipped.reused[0]={body,5};assert(dense.finish(*q,true,{},1001,false,skipped)&&dense.unchanged==1&&d.updated==1001);
        // The root changed and its re-read overruns: back to parts, old data kept.
        q=turn(dense,at,still,1800,home,false);assert(q&&q->part==nativeRegions::kWhole&&dense.finish(*q,false,{},1801,true));
        q=turn(dense,at,still,1802,home,false);assert(q&&q->key==key&&q->part==0&&!q->knownCount&&d.complete);
        assert(dense.finish(*q,true,{},1803,false,part(*q,6)));
        // A signature that differs between two parts restarts the set.
        q=turn(dense,at,still,1804,home,false);assert(q&&q->part==1&&dense.finish(*q,true,{},1805,false,part(*q,7)));
        assert(d.done.none()&&d.partialRoots.empty()&&!d.partsActive&&d.statics[0].signature==5&&d.partsOnly);
        // Resumable read: chunks in order assemble one root over several
        // harvests; the region is asked for again at once and never split.
        const uint32_t other=0x25454530;
        nativeRegions::Cache slow;slow.reset(13,13,1);
        job=turn(slow,at,still,100,home,true);assert(job&&!job->resume.body);
        auto chunk=[&](uint32_t index,bool partial,ColTri t){Roots r;r.fresh.push_back({body,9,{t},index,partial});r.continued=partial;return r;};
        Roots first=chunk(0,true,wall);first.fresh.insert(first.fresh.begin(),nativeRegions::StaticRoot{other,4,{loose}});
        assert(slow.finish(*job,false,{},101,true,first)&&!slow.splits&&!slow.unknown);
        auto& w=slow.entries.at(key);
        assert(w.continuing&&!w.complete&&w.assembling.body==body&&w.assembling.chunk==1&&w.statics.size()==1&&w.statics[0].body==other);
        // The root finished in that harvest is already held and named as known.
        job=turn(slow,at,still,102,home,true);assert(job&&job->key==key&&job->part==nativeRegions::kWhole&&job->resume.body==body&&job->resume.signature==9&&job->resumeChunk==1);
        assert(job->knownCount==1&&job->known[0].body==other);
        Roots second=chunk(1,true,moved);second.reused.push_back({other,4});
        assert(slow.finish(*job,false,{},103,true,second)&&w.assembling.chunk==2&&w.continuing&&slow.chunks==2);
        // A chunk out of order discards the assembly: the read starts again.
        job=turn(slow,at,still,104,home,true);assert(job->resumeChunk==2&&slow.finish(*job,false,{},105,true,chunk(3,true,wall)));
        assert(!w.assembling.body&&!w.continuing&&slow.chunkRestarts==1&&slow.unknown==1);
        job=turn(slow,at,still,900,home,true);assert(job&&job->key==key&&!job->resume.body&&slow.finish(*job,false,{},901,true,chunk(0,true,wall)));
        job=turn(slow,at,still,902,home,true);assert(job->resumeChunk==1);
        Roots last=chunk(1,false,moved);last.reused.push_back({other,4});
        assert(slow.finish(*job,true,{},903,false,last)&&w.complete&&!w.continuing&&!w.assembling.body&&w.updated==903);
        assert(w.statics.size()==2&&w.triangles.size()==3&&w.statics[0].body==body&&w.statics[0].triangles.size()==2&&w.statics[0].read==903&&!slow.splits);
        job=turn(slow,at,still,1700,home,true);assert(job->key==key&&job->knownCount==2&&!job->resume.body);
        // The last chunk without the earlier ones cannot complete the region.
        job->knownCount=0;assert(slow.finish(*job,true,{},1701,false,chunk(4,false,wall))&&w.updated==903&&w.statics[0].triangles.size()==2);
        // A harvest that stops again and again is eventually a failure.
        nativeRegions::Cache stuck;stuck.reset(14,14,1);uint64_t t=100;
        for(uint32_t i=0;;i++,t+=2){
            auto q=turn(stuck,at,still,t,home,true);assert(q&&q->key==key);
            Roots r;r.fresh.push_back({other,4,{loose}});r.continued=true;assert(stuck.finish(*q,false,{},t+1,true,r));
            if(!stuck.entries.at(key).continuing){assert(i+1==nativeRegions::kMaxContinues&&stuck.unknown==1);break;}
        }
    }
    // Native session: stage onto Link, ready only once covered, consumed,
    // acknowledged and grounded; coverage loss parks MC without a clear.
    {
        nativeStream::reset(6,8,0x1234,"check-session");nativeStream::publish(6000);
        uint64_t sessionHead=*reinterpret_cast<uint64_t*>(ring);*reinterpret_cast<uint64_t*>(ring+0x40)=sessionHead;
        BotwState sky{};sky.flags=kBotwInGame;nativeStream::configure(sky);assert(sky.flags&kBotwLoading); // nothing staged yet
        body::ready=true;body::readyAt=GetTickCount64();body::sampleAt=6000;body::latest={};body::latest.actor=1;
        double spawnMc[]{1,100.25,1};for(int k=0;k<3;k++)body::latest.observed[k]=float(nativeRegions::anchor[k]+(spawnMc[k]-nativeRegions::origin[k])*nativeRegions::scale);
        nativeStream::progress(6000);assert(nativeStream::staged&&!nativeStream::ready);
        sky={};sky.flags=kBotwInGame;nativeStream::configure(sky);
        assert((sky.flags&kBotwLoading)&&sky.teleportSeq==nativeStream::teleport&&std::abs(sky.posY-100.25)<1e-5); // uncovered spawn: parked
        double at[]{1,100.25,1},still[]{0,0,0};
        for(int i=0;i<27;i++){auto job=stream.next(at,still,6001+i);assert(job);
            std::vector<ColTri> tris;if(nativeGeometry::overlaps(floor,job->lo,job->hi))tris.push_back(floor);assert(stream.finish(*job,true,tris,6001+i));}
        for(int i=0;i<64;i++)nativeStream::publish(6100);
        nativeStream::progress(6100);sky={};sky.flags=kBotwInGame;nativeStream::configure(sky);
        assert(sky.flags&kBotwLoading); // published, not yet consumed by MC
        uint64_t sessionTail=*reinterpret_cast<uint64_t*>(ring);*reinterpret_cast<uint64_t*>(ring+0x40)=sessionTail;
        nativeStream::progress(6101);sky={};sky.flags=kBotwInGame;nativeStream::configure(sky);
        assert(!(sky.flags&kBotwLoading)&&!nativeStream::ready); // MC may teleport now
        live::mc={};live::mc.curX=live::mc.prevX=1;live::mc.curY=live::mc.prevY=100.25;live::mc.curZ=live::mc.prevZ=1;live::mc.tickMs=50;
        live::mc.flags=kMcInWorld|kMcOnGround;live::mc.teleportAck=nativeStream::teleport-1;
        nativeStream::progress(6102);assert(!nativeStream::ready); // stale teleport acknowledgement
        live::mc.teleportAck=nativeStream::teleport;live::mc.flags=kMcInWorld;nativeStream::progress(6103);assert(!nativeStream::ready); // airborne
        nativeStream::progress(6103+nativeStream::kSettleMs-1);assert(!nativeStream::ready); // airborne, not yet for kSettleMs
        live::mc.flags=kMcInWorld|kMcOnGround;nativeStream::progress(6104);assert(nativeStream::ready&&walking::ready);
        body::command=5; // Minecraft in control from here: coverage pauses, the native body follows
        uint64_t before=*reinterpret_cast<uint64_t*>(ring);
        live::mc.curX=live::mc.prevX=40;nativeStream::progress(6105);assert(nativeStream::paused&&nativeStream::pauses==1);
        // A coverage pause is a wait: Minecraft keeps its keys and velocity (kBotwHold), no loading park.
        sky={};sky.flags=kBotwInGame;nativeStream::configure(sky);assert((sky.flags&kBotwHold)&&!(sky.flags&kBotwLoading)&&nativeStream::parkPauses==0);
        nativeStream::publish(6106);assert(*reinterpret_cast<uint64_t*>(ring)==before&&!stream.entries.empty()); // no clear, data kept
        live::mc.curX=live::mc.prevX=1;nativeStream::progress(6107);assert(!nativeStream::paused&&nativeStream::resumes==1);
        sky={};sky.flags=kBotwInGame;nativeStream::configure(sky);assert(!(sky.flags&(kBotwLoading|kBotwHold)));
        // Travel lookahead into unknown space pauses before arrival; age pauses too.
        live::mc.prevX=7;live::mc.curX=7.75;nativeStream::progress(6108);assert(nativeStream::paused); // 15 MC/s: lookahead reaches unharvested x>=16
        // Waiting, Minecraft reports no velocity; it will continue at the one it had, so that way must be covered.
        live::mc.prevX=live::mc.curX=7.75;nativeStream::progress(6109);assert(nativeStream::paused&&nativeStream::holding);
        // The way ahead stays unreadable: after kHoldMs the wait becomes SkyCraft's park, and standing room is enough.
        nativeStream::progress(6108+nativeStream::kHoldMs);assert(!nativeStream::holding&&nativeStream::parkPauses==1);
        sky={};sky.flags=kBotwInGame;nativeStream::configure(sky);assert((sky.flags&kBotwLoading)&&!(sky.flags&kBotwHold));
        nativeStream::progress(6109+nativeStream::kHoldMs);assert(nativeStream::paused); // Minecraft has to see the park first
        nativeStream::progress(6108+nativeStream::kHoldMs+nativeStream::kParkMs);assert(!nativeStream::paused&&nativeStream::parkResumes==1);
        // lab/coverage-hold.txt 0: every pause is the park.
        nativeStream::lightHold=false;live::mc.prevX=7;live::mc.curX=7.75;nativeStream::progress(6110+nativeStream::kHoldMs+nativeStream::kParkMs);assert(nativeStream::paused&&!nativeStream::holding&&nativeStream::parkPauses==2);
        sky={};sky.flags=kBotwInGame;nativeStream::configure(sky);assert(sky.flags&kBotwLoading);
        nativeStream::lightHold=true;live::mc.prevX=live::mc.curX=1;nativeStream::progress(6111+nativeStream::kHoldMs+nativeStream::kParkMs);assert(!nativeStream::paused);
        nativeStream::progress(6050+nativeRegions::kMaxAgeMs);assert(nativeStream::paused);
        // A native menu is already parked by live.h (no kBotwInGame); never forced into play.
        sky={};sky.flags=kBotwMenuOpen;nativeStream::configure(sky);assert(sky.flags==kBotwMenuOpen);
        // Control off and Link moved away natively: Minecraft is staged onto him again, with a new teleport.
        uint64_t later=6050+nativeRegions::kMaxAgeMs;body::command=0;body::sampleAt=later+10;
        nativeStream::progress(later+10);assert(nativeStream::ready); // Link still where Minecraft stands
        double awayMc[]{4,100.25,1};for(int k=0;k<3;k++)body::latest.observed[k]=float(nativeRegions::anchor[k]+(awayMc[k]-nativeRegions::origin[k])*nativeRegions::scale);
        auto oldTeleport=nativeStream::teleport;body::sampleAt=later+300;
        nativeStream::progress(later+300);assert(!nativeStream::staged&&!nativeStream::ready&&!walking::ready);
        nativeStream::progress(later+301);assert(nativeStream::staged&&nativeStream::teleport!=oldTeleport&&std::abs(nativeStream::spawn[0]-4)<1e-3);
        // Hovering in creative flight counts as settled.
        live::mc.curX=live::mc.prevX=4;live::mc.teleportAck=nativeStream::teleport;live::mc.flags=kMcInWorld|kMcFlying;
        for(int i=0;i<27;i++){double there[]{4,100.25,1};auto job=stream.next(there,still,later+302+i);if(!job)break;
            std::vector<ColTri> tris;if(nativeGeometry::overlaps(floor,job->lo,job->hi))tris.push_back(floor);assert(stream.finish(*job,true,tris,later+302+i));}
        for(int i=0;i<64;i++)nativeStream::publish(later+340);
        *reinterpret_cast<uint64_t*>(ring+0x40)=*reinterpret_cast<uint64_t*>(ring);
        nativeStream::progress(later+341);assert(nativeStream::ready&&walking::ready);
        body::ready=false;body::latest={};live::mc={};
    }
    // Acquisition order at speed: the regions on the way first, in the order the body reaches them.
    {
        using nativeRegions::Key;
        nativeRegions::Cache c;c.reset(1,1);double at[]{1,100.25,1},fast[]{30,0,0};
        for(int x=0;x<=3;x++){auto job=c.next(at,fast,100+x);assert(job&&(job->key==Key{x,12,0}));assert(c.finish(*job,true,{},100+x));}
        // A request whose ray ended without a harvest is abandoned, and the region is chosen again at once.
        {auto job=c.next(at,fast,200);assert(job);auto key=job->key;c.abandon();auto again=c.next(at,fast,201);
            assert(again&&again->key==key&&again->id!=job->id&&c.skipped==1&&c.finish(*again,true,{},201));}
        // Without (lab/region-path.txt 0) the third read is a neighbour of the travel point, not the next region ahead.
        nativeRegions::pathOrder=false;nativeRegions::Cache plain;plain.reset(1,1);
        for(int i=0;i<3;i++){auto job=plain.next(at,fast,100+i);assert(job&&(i==0?job->key==Key{0,12,0}:i==1?job->key==Key{1,12,0}:job->key!=Key{2,12,0}));assert(plain.finish(*job,true,{},100+i));}
        nativeRegions::pathOrder=true;
    }
    // Support projection: SkyCraft's 13-sample footprint versus the centre.
    {
        NativeSupport plane;double g=.2; // 11.3 degree ramp rising along +x
        auto flat=[&](NativeSupport& s,double x0,double x1,double z0,double z1,double y0,double gx){
            auto h=[&](double x){return float(y0+gx*(x-x0));};
            s.tris.push_back({float(x0),h(x0),float(z0),float(x0),h(x0),float(z1),float(x1),h(x1),float(z1)});
            s.tris.push_back({float(x0),h(x0),float(z0),float(x1),h(x1),float(z1),float(x1),h(x1),float(z0)});};
        flat(plane,-4,4,-4,4,100,g);
        double x=.3,z=-.2,mcY=support::floor(plane,x,200,z,0,false); // what TriCollider stands on
        assert(std::abs(mcY-(100+g*(x+4)+.15*g))<1e-5);
        auto r=support::project(plane,x,mcY,z,true,.03/1.3);
        assert(r.kind==support::Plane&&std::abs(r.gap-.15*g)<1e-5&&std::abs(r.centre-(100+g*(x+4)))<1e-5);
        assert(support::project(plane,x,mcY+.5,z,false,.03/1.3).kind==support::Airborne);
        assert(support::project(plane,x,mcY+.5,z,true,.03/1.3).kind==support::Unexplained); // standing on an MC block
        NativeSupport steep;flat(steep,-4,4,-4,4,100,1.2); // 50 degrees: walkable at the BOTW limit (60), not at SkyCraft's (45)
        assert(support::project(steep,x,support::floor(steep,x,200,z,0,false),z,true,.03/1.3).kind==support::Plane);
        support::walkableNy=support::walkableFromDegrees(45);assert(support::walkableNy.load()==.7);
        assert(support::project(steep,x,support::floor(steep,x,200,z,0,false),z,true,.03/1.3).kind==support::Unexplained);
        support::walkableNy=support::walkableFromDegrees(60);assert(std::abs(support::walkableNy.load()-.5)<1e-12);
        NativeSupport wall;flat(wall,-4,4,-4,4,100,2); // 63 degrees: not walkable
        assert(support::project(wall,x,support::floor(wall,x,200,z,0,false),z,true,.03/1.3).kind==support::Unexplained);
        NativeSupport ledge;flat(ledge,-4,0,-4,4,101,0);flat(ledge,0,4,-4,4,100,0);
        assert(support::floor(ledge,.1,200,0,0,false)==101); // footprint reaches the ledge
        r=support::project(ledge,.1,101,0,true,.03/1.3);assert(r.kind==support::Unexplained&&r.gap==0); // edge keeps MC feet
        NativeSupport crease;flat(crease,-4,0,-4,4,98,.5);flat(crease,0,4,-4,4,100,-.5); // ridge: centre plane cannot explain the footprint
        r=support::project(crease,.01,support::floor(crease,.01,200,0,0,false),0,true,.03/1.3);assert(r.kind==support::Unexplained);
        // interpolateTarget: camera feet stay logical MC; only the body origin moves.
        LiveTarget t;t.source.seq=2;t.source.tickMs=50;t.source.flags=kMcInWorld|kMcOnGround;t.source.tickEye=t.source.tickEyeO=1.62f;
        t.source.prevX=t.source.curX=x;t.source.prevY=t.source.curY=mcY;t.source.prevZ=t.source.curZ=z;
        LARGE_INTEGER q;QueryPerformanceCounter(&q);t.source.tickQpc=q.QuadPart;t.receivedQpc=q.QuadPart;
        t.scale=nativeRegions::scale;std::copy_n(nativeRegions::origin,3,t.origin);std::copy_n(nativeRegions::anchor,3,t.anchor);
        t.unbounded=true;t.native=std::make_shared<const NativeSupport>(plane);AppliedTarget out;
        LARGE_INTEGER frequency;QueryPerformanceFrequency(&frequency);
        assert(interpolateTarget(t,q.QuadPart,frequency.QuadPart,out)&&out.support.kind==support::Plane);
        assert(std::abs(out.feet[1]-(nativeRegions::anchor[1]+(mcY-100)*1.3))<1e-3&&std::abs(out.feet[1]-out.body[1]-.15*g*1.3)<1e-4);
        assert(out.feet[0]==out.body[0]&&out.feet[2]==out.body[2]);
        t.native=nullptr;assert(interpolateTarget(t,q.QuadPart,frequency.QuadPart,out)&&out.body[1]==out.feet[1]);
        {
            // Tick history: Minecraft moves 0.3 a tick (20 Hz); this side samples at 30 Hz and
            // first sees each tick 20-55 ms after its stamp, as measured in route-42. After the
            // delay has settled every frame advances by the same distance (no 20 Hz steps), the
            // sampled position is never ahead of the newest tick, and a stop does not overshoot.
            tickClock::reset();const int64_t f=10000000;double worstStep=0,least=1e9,most=0,last=NAN;int late0=int(tickClock::lateFrames.load());
            auto state=[&](int n,bool moving){botwcraft::proto::McState s{};s.seq=2;s.tickMs=50;s.flags=kMcInWorld;s.tickQpc=1000000+int64_t(n)*500000;
                double a=moving?.3*(n-1):.3*std::min(n-1,199),b=moving?.3*n:.3*std::min(n,199);s.prevX=a;s.curX=b;s.prevY=s.curY=100;s.tickEye=s.tickEyeO=1.62f;return s;};
            for(int frame=0;frame<400;frame++){
                int64_t now=1000000+int64_t(frame)*333333+200000;
                // Newest tick whose stamp is at least its arrival latency old (20..55 ms, varying).
                int n=0;while(1000000+int64_t(n+1)*500000+(200000+((n+1)*7919%36)*10000)<=now)n++;
                if(n<1)continue;
                double feet[3],eye,ticks;botwcraft::proto::McState used;assert(tickClock::sample(state(n,frame<300),now,f,feet,eye,used,ticks));
                assert(feet[0]<=state(n,frame<300).curX+1e-9);
                if(frame>=150&&frame<290&&std::isfinite(last)){double step=feet[0]-last;least=std::min(least,step);most=std::max(most,step);worstStep=std::max(worstStep,std::abs(step-.3*33.3333/50));}
                if(frame>=300)assert(feet[0]<=.3*199+1e-9);
                last=feet[0];
            }
            printf("tick history: steady 6 units/s sampled at 30 Hz with ticks seen 20-55 ms late: frame step %.4f..%.4f (ideal 0.2000), delay %.1f ms, late frames %d\n",least,most,tickClock::delayMs,int(tickClock::lateFrames.load())-late0);
            assert(worstStep<.004&&tickClock::delayMs>=tickClock::kMinDelayMs&&tickClock::delayMs<=tickClock::kMaxDelayMs);
            tickClock::reset();
        }
    }
    {
        // The marker's frame number through Cemu's clear (the game's float to bytes, bytes back to a float, both
        // single precision): every number of six days of play at 30 frames a second, the old reading and the new.
        auto cemu=[](float a){volatile float bytes=a*255.0f;uint32_t q=uint32_t(bytes);volatile float back=float(q)/255.0f;return float(back);};
        uint32_t wrongWhole=0,firstWrong=0,wrongLow=0;
        for(uint32_t n=1;n<(1u<<24);n++){
            if(uint32_t(cemu(float(n)))!=n){if(!wrongWhole)firstWrong=n;++wrongWhole;}
            markerSequence.store(n+n%7);   // the game up to six frames ahead of the renderer
            if(markerFrame(cemu(float(n&kMarkerLow)))!=n)++wrongLow;
        }
        markerSequence.store(0);
        printf("marker frame: of 2^24 numbers the whole one comes back wrong %u times (first %u), the low 15 bits %u\n",wrongWhole,firstWrong,wrongLow);
        assert(firstWrong==65795&&wrongLow==0);
    }
    {
        // Convex pieces: the hull's faces of a box's corners, of a tetrahedron, of a box with points within it, in
        // its faces and along its edges, of a wedge; no solid of points in one plane. Volume by the triangles,
        // which is positive only when every one of them is wound outward.
        auto volume=[](const std::vector<std::array<float,9>>& t){double v=0;for(const auto& a:t)v+=(a[0]*(double(a[4])*a[8]-double(a[5])*a[7])-a[1]*(double(a[3])*a[8]-double(a[5])*a[6])+a[2]*(double(a[3])*a[7]-double(a[4])*a[6]))/6;return v;};
        std::vector<std::array<float,9>> out;
        std::vector<float> box;for(int i=0;i<8;i++){box.push_back(i&1?3:1);box.push_back(i&2?5:2);box.push_back(i&4?4:0);}   // 2 x 3 x 4
        assert(nativeShapes::hullTriangles(box.data(),8,out)&&out.size()==12&&std::abs(volume(out)-24)<1e-3);
        out.clear();const float tetra[12]{0,0,0,1,0,0,0,1,0,0,0,1};
        assert(nativeShapes::hullTriangles(tetra,4,out)&&out.size()==4&&std::abs(volume(out)-1./6)<1e-5);
        out.clear();auto more=box;for(float v:{2.f,3.5f,2.f, 1.f,3.5f,2.f, 1.f,2.f,2.f, 3.f,5.f,1.f, 2.f,2.f,0.f, 1.f,2.f,0.f})more.push_back(v);
        assert(nativeShapes::hullTriangles(more.data(),uint32_t(more.size()/3),out)&&out.size()==12&&std::abs(volume(out)-24)<1e-3);
        out.clear();const float wedge[18]{0,0,0,4,0,0,0,0,2,4,0,2,0,3,0,0,3,2};   // a ramp: 4 long, 3 high, 2 wide
        assert(nativeShapes::hullTriangles(wedge,6,out)&&out.size()==8&&std::abs(volume(out)-12)<1e-3);
        // The same box where the shrine stands: the world's large numbers must not cost a face its plane.
        out.clear();auto distant=box;for(size_t i=0;i<distant.size();i+=3){distant[i]+=-470.6f;distant[i+1]+=177.7f;distant[i+2]+=1990.6f;}
        assert(nativeShapes::hullTriangles(distant.data(),8,out)&&out.size()==12);
        for(auto& a:out)for(int j=0;j<9;j++)a[j]-=j%3==0?-470.6f:j%3==1?177.7f:1990.6f;
        assert(std::abs(volume(out)-24)<5e-2);
        out.clear();const float plane[12]{0,0,0,1,0,0,0,1,0,1,1,0};
        assert(!nativeShapes::hullTriangles(plane,4,out)&&out.empty());
        // A turned and moved box through the transform code: a rotation of 90 degrees about y, as the shrine's.
        nativeShapes::Affine turn;turn.c[0][0]=0;turn.c[0][2]=1;turn.c[2][0]=-1;turn.c[2][2]=0;turn.t[0]=10;
        nativeShapes::Affine child;child.t[1]=2;auto both=nativeShapes::compose(turn,child);
        float in[3]{1,0,0},at[3];nativeShapes::apply(both,in,at);assert(std::abs(at[0]-10)<1e-6&&std::abs(at[1]-2)<1e-6&&std::abs(at[2]-1)<1e-6);
        puts("convex pieces: hulls of a box, a tetrahedron, a box with idle points, a wedge; a plane refused; transforms composed");
    }
    assert(VirtualFree(live::base,0,MEM_RELEASE));live::base=nullptr;
    assert(VirtualFree(guest,0,MEM_RELEASE));
    puts("PASS: trace rotation; nested PPC state/locks; island groups/count guards/empty anchor; transforms/key dedup; 256-key canary/saturation; layer policy/virtual bounds/fallback/completeness; cache/epochs/ack; SAT/voxels/terrain; protocol11; native session stage/ready/pause/resume; footprint support projection; oriented box; static reuse signature/skip/audit/split; resumable reads; neighbour-first scheduling; decoded-root key filtering");
}
