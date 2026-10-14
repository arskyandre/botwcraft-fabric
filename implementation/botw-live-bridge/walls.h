// Roadmap step 5 (report 59): Minecraft's solid blocks as ground and walls for BOTW.
//
// The mod already sends, with every section of blocks it draws, which of its 4096 blocks are solid
// (kRenSolids: a bitset, bit x + 16z + 256y; a count of 0 takes the section's away). Here each
// section's blocks are merged into boxes (runs along x, widened along z, then raised along y; the
// boxes of a section do not depend on where the player is, so they stay the same while he moves),
// and for every box within kNear blocks of the player the game is asked for one AirWall of that
// size: its own invisible box, on the layer that holds whatever the ground holds (report 56). A
// box farther than kFar blocks, or whose blocks changed, is sent away (BaseProc::deleteLater).
//
// Each wall has a record and a map entry of its own in the pack's memory (spawn.h: kWalls slots).
// The record's +0x24 is the object the game made for it and
// is cleared by the game when the object is gone: that is how a slot is known to be standing or
// free again.
//
// The collision harvest leaves these boxes out (mine(), from nativeShapes): Minecraft's player
// already collides with the blocks themselves, and a click must end on the block, not on BOTW's
// copy of it.
// The game's notice at an AirWall is left out for these (notice(), below).
// lab/walls.txt 0: no walls (those standing are sent away).
// No walls either while the pattern for asking is not this map's (actors::airWallPattern): inside a shrine,
// whose map has no AirWall of its own, Minecraft's blocks are not solid for BOTW's things (route-203's crash).
#pragma once
namespace walls {
namespace p=botwcraft::proto;
constexpr int kMost=int(spawn::kWalls);constexpr float kNear=12,kFar=16;   // blocks from the player
struct Box {int lo[3],hi[3];bool operator==(const Box&) const=default;};
struct Section {std::array<uint64_t,64> bits{};std::vector<Box> boxes;};
static std::mutex mutex;static std::map<std::array<int,3>,Section> sections;
struct Wall {Box box{};int state=0;uint64_t since=0;uint32_t proc=0,id=0;};   // 0 free, 1 asked for, 2 standing, 3 asked to go
static Wall wall[kMost];static std::atomic<float> centre[kMost][3];static std::atomic<uint8_t> marked[kMost];
static std::atomic<bool> on{true};static uint32_t askedFor=0,stood=0,sentAway=0,neverCame=0;
// One of the bridge's own walls stands at that place (BOTW's coordinates)? From the collision harvest, on the game's thread.
static bool mine(const float* at){
    for(int i=0;i<kMost;i++)if(marked[i].load(std::memory_order_relaxed)){
        bool same=true;for(int k=0;k<3&&same;k++)same=std::abs(centre[i][k].load(std::memory_order_relaxed)-at[k])<.05f;
        if(same)return true;
    }
    return false;
}
// The AirWall's own action (AirWallAction, its calc at 020BD968) looks through its body's contacts and, when one is
// the player's, asks for the game's notice 0x2E, "You can't go any farther." (020BDBB4: li r3, 0x2E; bl 030731EC):
// the map's own AirWalls are the world's border. The pack calls here in place of that request (r4 the wall, the
// action's actor): r12 = 1 to ask as the game does, 0 for a wall of the bridge's, which is a block of Minecraft's
// (route-166: the notice came up whenever Steve stood on a block).
static std::atomic<uint64_t> noticesKept{0},noticesLeft{0};
static void notice(CPU* c){
    c->ip=c->spr.lr;uint32_t actor=c->gpr[4];uint8_t m[0x30];c->gpr[12]=1;
    if(actor<0x10000000||!actors::rd(actor+0x1F8,m,sizeof(m))){++noticesKept;return;}
    float at[3];for(int i=0;i<3;i++)at[i]=actors::bef(m+12+i*16);
    if(mine(at)){c->gpr[12]=0;++noticesLeft;}else ++noticesKept;
}
static void merge(Section& s,const int* origin){
    s.boxes.clear();static bool used[4096];memset(used,0,sizeof(used));
    auto get=[&](int x,int y,int z){int b=x+16*z+256*y;return ((s.bits[b>>6]>>(b&63))&1)&&!used[b];};
    for(int y=0;y<16;y++)for(int z=0;z<16;z++)for(int x=0;x<16;x++){
        if(!get(x,y,z))continue;
        int x1=x+1;while(x1<16&&get(x1,y,z))++x1;
        int z1=z+1;for(;z1<16;z1++){bool all=true;for(int i=x;i<x1&&all;i++)all=get(i,y,z1);if(!all)break;}
        int y1=y+1;for(;y1<16;y1++){bool all=true;for(int k=z;k<z1&&all;k++)for(int i=x;i<x1&&all;i++)all=get(i,y1,k);if(!all)break;}
        for(int j=y;j<y1;j++)for(int k=z;k<z1;k++)for(int i=x;i<x1;i++)used[i+16*k+256*j]=true;
        s.boxes.push_back({{origin[0]+x,origin[1]+y,origin[2]+z},{origin[0]+x1,origin[1]+y1,origin[2]+z1}});
    }
}
// kRenSolids, from the render stream's reader (the worker): a section's solid blocks, or none.
static void section(const p::RenSolids& a,const uint8_t* bits){
    std::lock_guard lock(mutex);std::array<int,3> key{a.sx,a.sy,a.sz};
    if(!a.count){sections.erase(key);return;}
    auto& s=sections[key];memcpy(s.bits.data(),bits,512);int origin[]{a.sx*16,a.sy*16,a.sz*16};merge(s,origin);
}
static void clear(){std::lock_guard lock(mutex);sections.clear();}
static void poll(){
    static uint64_t checked=0,read=0,told=0;auto now=GetTickCount64();if(now-checked<100)return;checked=now;
    static uint32_t filterTold=~0u;
    if(now-read>=1000){read=now;int value=1;std::ifstream(directory+"/walls.txt")>>value;on=value!=0;uint32_t wanted=kFilter;std::ifstream(directory+"/wall-filter.txt")>>std::hex>>wanted;filter=wanted;}
    float at[3];bool world=on.load()&&live::base&&live::connected&&!live::failed&&physicalActive.load()&&actors::playerAt(at)&&actors::airWallPattern();
    uint32_t room=spawn::wallRoom.load();
    // What became of what was asked.
    for(int i=0;i<kMost;i++){
        auto& w=wall[i];if(!w.state)continue;
        uint32_t proc=0;if(room)actors::word(room+uint32_t(i)*spawn::kWallBytes+0x24,proc);
        if(w.state==1){
            if(proc>=0x10000000){w.state=2;w.proc=proc;w.id=0;actors::word(proc+0x50,w.id);++stood;}
            else if(now-w.since>8000){w.state=0;marked[i]=0;++neverCame;}
        }else if(w.state==2){if(!proc){w.state=0;marked[i]=0;}}   // the game took it away itself
        else if(w.state==3){
            if(!proc){w.state=0;marked[i]=0;}
            else if(now-w.since>5000){spawn::Request r{};r.remove=-1;r.gone=w.proc;r.goneId=w.id;spawn::ask(r);w.since=now;}
        }
    }
    // The boxes wanted: near ones are asked for, those within the far range are kept.
    std::vector<Box> keep,want;
    if(world){
        double mc[3];for(int i=0;i<3;i++)mc[i]=(at[i]-nativeRegions::anchor[i])/nativeRegions::scale+nativeRegions::origin[i];
        std::lock_guard lock(mutex);
        for(auto& [key,s]:sections)for(auto& b:s.boxes){
            double d2=0;for(int i=0;i<3;i++){double d=mc[i]<b.lo[i]?b.lo[i]-mc[i]:mc[i]>b.hi[i]?mc[i]-b.hi[i]:0;d2+=d*d;}
            if(d2<=kFar*kFar)keep.push_back(b);
            if(d2<=kNear*kNear)want.push_back(b);
        }
    }
    int changes=0;
    for(int i=0;i<kMost;i++){
        auto& w=wall[i];if(w.state!=2)continue;
        if(std::find(keep.begin(),keep.end(),w.box)!=keep.end())continue;
        spawn::Request r{};r.remove=-1;r.gone=w.proc;r.goneId=w.id;spawn::ask(r);w.state=3;w.since=now;++sentAway;++changes;
    }
    for(auto& b:want){
        if(changes>=16)break;   // a few each time: the game is asked four an update
        bool have=false;int free=-1;
        for(int i=0;i<kMost;i++){if(wall[i].state&&wall[i].box==b){have=true;break;}if(!wall[i].state&&free<0)free=i;}
        if(have||free<0)continue;
        spawn::Request r{};r.remove=-1;strncpy(r.name,"AirWall",spawn::kNameBytes-1);r.map=true;r.wall=free+1;r.scaled=true;
        strncpy(r.key[0],"Shape",spawn::kNameBytes-1);strncpy(r.text[0],"Box",spawn::kNameBytes-1);r.extras=1;
        for(int k=0;k<3;k++){
            r.at[k]=nativeRegions::anchor[k]+(float(b.lo[k]+b.hi[k])*.5f-nativeRegions::origin[k])*nativeRegions::scale;
            r.scale[k]=float(b.hi[k]-b.lo[k])*.5f*nativeRegions::scale;centre[free][k]=r.at[k];
        }
        marked[free]=1;wall[free]={b,1,now,0,0};spawn::ask(r);++askedFor;++changes;
    }
    if(changes||now-told>=5000){
        told=now;int count[4]{};for(auto& w:wall)++count[w.state];size_t held=0,boxes=0;{std::lock_guard lock(mutex);held=sections.size();for(auto& [key,s]:sections)boxes+=s.boxes.size();}
        static int last[4]{-1};static size_t lastBoxes=~size_t(0);
        static uint64_t lastLeft=0;uint64_t left=noticesLeft.load();
        if(changes||memcmp(last,count,sizeof(count))||lastBoxes!=boxes||(left!=lastLeft&&(lastLeft=left,true))||filterTold!=filter.load()){
            filterTold=filter.load();
            memcpy(last,count,sizeof(count));lastBoxes=boxes;
            event("{\"event\":\"walls\",\"world\":%d,\"sections\":%u,\"boxes\":%u,\"wanted\":%u,\"asked\":%d,\"standing\":%d,\"going\":%d,\"asked_in_all\":%u,\"stood_in_all\":%u,\"sent_away_in_all\":%u,\"never_came\":%u,\"room\":%u,\"notices_left_out\":%llu,\"notices_of_the_games_walls\":%llu,\"pattern\":%d,\"screens\":%u,\"filter\":%u,\"refiltered\":%u}",
                int(world),unsigned(held),unsigned(boxes),unsigned(want.size()),count[1],count[2],count[3],askedFor,stood,sentAway,neverCame,room,(unsigned long long)noticesLeft.load(),(unsigned long long)noticesKept.load(),int(actors::airWallPattern()),body::stageScreens.load(),filter.load(),refiltered.load());
        }
    }
}
}
