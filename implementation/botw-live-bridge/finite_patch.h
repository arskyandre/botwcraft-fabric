// Finite measured patch, worker-owned staging, unchanged protocol11 collision ring.
#pragma once
#include <set>
namespace walking {
namespace p=botwcraft::proto;
struct Packet {uint32_t type;std::vector<uint8_t> bytes;};
struct Patch {float scale=0,origin[3]{},anchor[3]{},bounds[4]{},vertical[2]{-1,3};
    std::vector<Packet> packets;std::shared_ptr<const SupportDomain> support;};
static Patch patch;
static uint64_t bodyEpoch=0,peerSession=0,publishedHead=0,lastClaim=0;
static uint32_t epoch=1,teleport=1;
static size_t nextPacket=0;
static bool staged=false,published=false,ready=false;
static double spawn[3]{0,100,0};
static bool read(std::istream& file,Patch& result){
    uint32_t magic=0,count=0;
    file.read(reinterpret_cast<char*>(&magic),4);file.read(reinterpret_cast<char*>(&count),4);
    bool extended=magic==0x32435046;
    if((!extended&&magic!=0x31435046)||(!extended&&count!=25)||count<3||count>513||!(count&1))return false;
    float values[11];file.read(reinterpret_cast<char*>(values),sizeof(values));if(!file)return false;
    for(float v:values)if(!std::isfinite(v)||std::abs(v)>100000)return false;
    result={};result.scale=values[0];if(result.scale<=0||result.scale>=10)return false;
    std::copy_n(values+1,3,result.origin);std::copy_n(values+4,3,result.anchor);std::copy_n(values+7,4,result.bounds);
    if(extended){file.read(reinterpret_cast<char*>(result.vertical),8);if(!file)return false;}
    for(float v:result.vertical)if(!std::isfinite(v)||std::abs(v)>32)return false;
    if(result.vertical[0]>=result.vertical[1])return false;
    int envelope[6]{-8,88,-8,7,111,7},dimensions[3]{2,3,2};
    if(extended){
        file.read(reinterpret_cast<char*>(envelope),sizeof(envelope));if(!file)return false;
        uint64_t regionCount=1;
        for(int k=0;k<3;k++){
            if(envelope[k]%8||(int64_t(envelope[k+3])+1)%8||envelope[k]>envelope[k+3]||
                int64_t(envelope[k+3])-envelope[k]>=64||std::abs(int64_t(envelope[k]))>100000)return false;
            dimensions[k]=int((int64_t(envelope[k+3])-envelope[k]+1)/8);regionCount*=dimensions[k];
        }
        if(count!=1+2*regionCount)return false;
        double lo[]={result.origin[0]+result.bounds[0]/result.scale,result.origin[1]+result.vertical[0]/result.scale,result.origin[2]+result.bounds[2]/result.scale};
        double hi[]={result.origin[0]+result.bounds[1]/result.scale,result.origin[1]+result.vertical[1]/result.scale+3,result.origin[2]+result.bounds[3]/result.scale};
        for(int k=0;k<3;k++)if(lo[k]<envelope[k]+1||hi[k]>envelope[k+3])return false;
    }
    float span=extended?64:8;
    if(result.bounds[1]-result.bounds[0]<=1||result.bounds[3]-result.bounds[2]<=1||result.bounds[1]-result.bounds[0]>span||result.bounds[3]-result.bounds[2]>span)return false;
    p::ColRegion previous{};
    std::set<std::array<int,3>> regions;uint64_t totalBytes=0,totalTriangles=0;
    auto domain=std::make_shared<SupportDomain>();
    for(uint32_t i=0;i<count;i++){
        uint32_t type=0,n=0;file.read(reinterpret_cast<char*>(&type),4);file.read(reinterpret_cast<char*>(&n),4);
        totalBytes+=n;
        if(n>(1u<<20)||totalBytes>(8u<<20)||type!=(i==0?1:i%2?3:2))return false;
        Packet packet{type,std::vector<uint8_t>(n)};file.read(reinterpret_cast<char*>(packet.bytes.data()),n);if(!file)return false;
        if(i==0){if(n!=4||packet.bytes!=std::vector<uint8_t>(4))return false;}
        else {
            if(n<32)return false;
            p::ColRegion r;memcpy(&r,packet.bytes.data(),32);
            auto region=(i-1)/2;
            if(r.minX!=envelope[0]+int(region/(dimensions[1]*dimensions[2]))*8||
                r.minY!=envelope[1]+int(region/dimensions[2]%dimensions[1])*8||r.minZ!=envelope[2]+int(region%dimensions[2])*8)return false;
            if(r.epoch||r.maxX!=r.minX+7||r.maxY!=r.minY+7||r.maxZ!=r.minZ+7||r.minX%8||r.minY%8||r.minZ%8||
                (!extended&&(r.minX < -8||r.maxX>7||r.minY<88||r.maxY>111||r.minZ < -8||r.maxZ>7))||
                n!=32+uint64_t(r.count)*(type==3?40:80))return false;
            if(type==3){
                if(!regions.insert({r.minX,r.minY,r.minZ}).second||(totalTriangles+=r.count)>65536)return false;
                for(uint32_t j=0;j<r.count;j++){p::ColTri t;memcpy(&t,packet.bytes.data()+32+j*40,40);for(float v:t.v)if(!std::isfinite(v))return false;
                    if(extended)for(int k=0;k<9;k++)if(t.v[k]<envelope[k%3]||t.v[k]>envelope[k%3+3]+1)return false;
                    float a[3],b[3],normal[3];for(int k=0;k<3;k++){a[k]=t.v[k+3]-t.v[k];b[k]=t.v[k+6]-t.v[k];}
                    for(int k=0;k<3;k++)normal[k]=a[(k+1)%3]*b[(k+2)%3]-a[(k+2)%3]*b[(k+1)%3];
                    float area=normal[0]*normal[0]+normal[1]*normal[1]+normal[2]*normal[2];
                    if(t.flags||!std::isfinite(area)||area<1e-12f)return false;
                    if(extended&&normal[1]>0&&normal[1]*normal[1]>=.49f*area){
                        auto& bin=domain->floors[{r.minX/8,r.minZ/8}];
                        std::array<float,9> vertices;std::copy_n(t.v,9,vertices.begin());
                        if(std::find(bin.begin(),bin.end(),vertices)==bin.end())bin.push_back(vertices);
                        if(bin.size()>2048)return false;
                    }}
                previous=r;
            }else {
                if(memcmp(&r,&previous,24))return false;
                if(r.count>512)return false;
                std::set<std::array<int,3>> blocks;
                for(uint32_t j=0;j<r.count;j++){p::ColBlock b;memcpy(&b,packet.bytes.data()+32+j*80,80);if(b.pad||b.x<r.minX||b.x>r.maxX||b.y<r.minY||b.y>r.maxY||b.z<r.minZ||b.z>r.maxZ||!blocks.insert({b.x,b.y,b.z}).second)return false;}
            }
        }
        result.packets.push_back(std::move(packet));
    }
    if(extended){if(domain->floors.empty())return false;result.support=domain;}
    return file.peek()==std::char_traits<char>::eof();
}
static bool load(Patch& result){std::ifstream file(directory+"/finite-patch.bin",std::ios::binary);return read(file,result);}
static bool aboveFloor(const Patch& p,const double* pos){
    for(const auto& packet:p.packets)if(packet.type==3){
        botwcraft::proto::ColRegion r;memcpy(&r,packet.bytes.data(),32);
        for(uint32_t i=0;i<r.count;i++){
            botwcraft::proto::ColTri t;memcpy(&t,packet.bytes.data()+32+i*40,40);
            const float* a=t.v;const float* b=t.v+3;const float* c=t.v+6;
            double d=(b[2]-c[2])*(a[0]-c[0])+(c[0]-b[0])*(a[2]-c[2]);
            if(d>=-1e-6)continue; // +Y floor winding; wall projections are rejected below.
            double u=((b[2]-c[2])*(pos[0]-c[0])+(c[0]-b[0])*(pos[2]-c[2]))/d;
            double v=((c[2]-a[2])*(pos[0]-c[0])+(a[0]-c[0])*(pos[2]-c[2]))/d;
            double height=u*a[1]+v*b[1]+(1-u-v)*c[1];
            double nx=(b[1]-a[1])*(c[2]-a[2])-(b[2]-a[2])*(c[1]-a[1]);
            double ny=-d,nz=(b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0]);
            if(std::min({u,v,1-u-v})>=-1e-6&&ny*ny>=.49*(nx*nx+ny*ny+nz*nz)&&pos[1]>=height-.03/p.scale&&pos[1]<=height+3/p.scale)return true;
        }
    }
    return false;
}
static void invalidate(const char* reason){
    if(!staged)return;
    staged=published=ready=false;liveBodyLease=false;liveTarget.store(nullptr);appliedTarget.store(nullptr);
    liveCollisionEpoch=++epoch;physicalBlocked=true;
    if(body::command.load()==5)body::invalidate(reason);
    event("{\"event\":\"collision_invalidated\",\"epoch\":%u,\"reason\":\"%s\"}",epoch,reason);
}
static bool push(const Packet& packet){
    if(!live::base)return false;
    auto ring=live::base+p::kOffCollisionRing;
    auto& h=*reinterpret_cast<uint64_t*>(ring);auto& t=*reinterpret_cast<uint64_t*>(ring+0x40);
    uint64_t head=live::atom(h).load(),tail=live::atom(t).load(std::memory_order_acquire),capacity=p::kColRingDataBytes;
    if(head<tail||head-tail>capacity){live::fault("collision-ring-distance");return false;}
    uint64_t span=(8ull+packet.bytes.size()+7)&~7ull,pos=head%capacity,pad=pos+span>capacity?capacity-pos:0;
    if(span+pad>capacity-(head-tail))return false;
    if(pad){p::ColMsgHeader header{};memcpy(ring+0x80+pos,&header,8);head+=pad;pos=0;}
    p::ColMsgHeader header{packet.type,uint32_t(packet.bytes.size())};memcpy(ring+0x80+pos,&header,8);
    memcpy(ring+0x88+pos,packet.bytes.data(),packet.bytes.size());memcpy(ring+0x88+pos+(packet.type==1?0:24),&epoch,4);
    live::atom(h).store(head+span,std::memory_order_release);publishedHead=head+span;
    if(packet.type==p::kColClear)++trace::collisionClears;
    if(packet.type!=p::kColBody)event("{\"event\":\"collision_publish\",\"epoch\":%u,\"type\":%u,\"packet\":%zu,\"head\":%llu,\"bytes\":%zu}",epoch,packet.type,nextPacket,publishedHead,packet.bytes.size());return true;
}
static void poll(){
    if(nativeRegions::enabled.load())return;
    if(staged&&(!live::connected||live::failed||!body::allowed()||bodyEpoch!=body::lifecycleEpoch.load()||peerSession!=live::session))invalidate("collision-lifecycle");
    uint64_t token=0;std::ifstream request(directory+"/walking-claim.txt");request>>token;
    if(token&&token!=lastClaim){
        lastClaim=token;Patch candidate;
        if(!physicalActive.load()&&live::connected&&!live::failed&&body::allowed()&&load(candidate)&&!body::busy.test_and_set()){
            auto pose=body::latest;body::busy.clear();bool valid=pose.actor&&GetTickCount64()-body::sampleAt.load()<250;
            for(int i=0;i<3;i++)spawn[i]=(pose.observed[i]-candidate.anchor[i])/candidate.scale+candidate.origin[i];
            float x=pose.observed[0]-candidate.anchor[0],z=pose.observed[2]-candidate.anchor[2];
            valid&=x>=candidate.bounds[0]+.5&&x<=candidate.bounds[1]-.5&&z>=candidate.bounds[2]+.5&&z<=candidate.bounds[3]-.5;
            valid&=pose.observed[1]>=candidate.anchor[1]+candidate.vertical[0]&&pose.observed[1]<=candidate.anchor[1]+candidate.vertical[1];
            valid&=aboveFloor(candidate,spawn);
            if(candidate.support){double margin=.5/candidate.scale;
                for(auto offset:std::array<std::array<double,2>,5>{{{0,0},{margin,0},{-margin,0},{0,margin},{0,-margin}}})
                    valid&=candidate.support->covers(spawn[0]+offset[0],spawn[2]+offset[1]);}
            if(valid){
                patch=std::move(candidate);bodyEpoch=body::lifecycleEpoch.load();peerSession=live::session;liveCollisionEpoch=++epoch;teleport=epoch+100;
                nextPacket=0;staged=true;published=ready=false;liveBodyLease=false;
                event("{\"event\":\"collision_stage\",\"epoch\":%u,\"body_epoch\":%llu,\"session\":%llu,\"teleport\":%u,\"spawn\":[%.12g,%.12g,%.12g]}",epoch,bodyEpoch,peerSession,teleport,spawn[0],spawn[1],spawn[2]);
            }
        }
    }
    if(!staged)return;
    for(unsigned budget=0;nextPacket<patch.packets.size()&&budget<4;budget++){
        if(!push(patch.packets[nextPacket]))break;
        ++nextPacket;
    }
    published=nextPacket==patch.packets.size();
    auto tail=live::atom(*reinterpret_cast<uint64_t*>(live::base+p::kOffCollisionRing+0x40)).load(std::memory_order_acquire);
    if(!ready&&published&&tail>=publishedHead&&live::mc.teleportAck==teleport&&(live::mc.flags&p::kMcOnGround)){
        LARGE_INTEGER q;QueryPerformanceCounter(&q);
        ready=true;event("{\"event\":\"collision_ready\",\"qpc\":%llu,\"epoch\":%u,\"tail\":%llu,\"ack\":%u,\"mc_seq\":%u,\"flags\":%u}",(uint64_t)q.QuadPart,epoch,tail,live::mc.teleportAck,live::mc.seq,live::mc.flags);
    }
}
static void configure(p::BotwState& sky){
    if(nativeRegions::enabled.load()){nativeStream::configure(sky);return;}
    if(!lastClaim)return; // Preserve stationary diagnostics until an explicit F claim.
    sky.collisionEpoch=epoch;sky.teleportSeq=teleport;
    sky.posX=spawn[0];sky.posY=spawn[1];sky.posZ=spawn[2];
    if(!staged||!published)sky.flags=p::kBotwMenuOpen;
}
static void target(){
    if(nativeRegions::enabled.load()){nativeStream::target();return;}
    if(!ready||!staged||!physicalActive.load()||physicalBlocked.load()){liveBodyLease=false;return;}
    LiveTarget value;value.source=live::mc;value.session=live::session;value.bodyEpoch=bodyEpoch;value.collisionEpoch=epoch;
    LARGE_INTEGER q;QueryPerformanceCounter(&q);value.receivedQpc=q.QuadPart;value.until=GetTickCount64()+250;
    value.scale=patch.scale;std::copy_n(patch.origin,3,value.origin);std::copy_n(patch.anchor,3,value.anchor);std::copy_n(patch.bounds,4,value.bounds);
    std::copy_n(patch.vertical,2,value.vertical);value.support=patch.support;
    liveTarget.store(std::make_shared<const LiveTarget>(value));liveBodyLease=true;
}
}
