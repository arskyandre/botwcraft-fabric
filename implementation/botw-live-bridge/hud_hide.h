// Parts of the game's HUD are hidden while Minecraft has the player (hidden from
// here, not with changed game files): what a button does now ("A Examine"; with the
// keyboard the letters are wrong), the hints of the controls of a new game (they stand over Minecraft's hotbar),
// and the guide of the directional pad at the top left (it showed the wrong rune). No file of the game's is
// changed and none is needed: the game's own layout objects are found in its memory by their names and the
// panes' "visible" bits are cleared, which is what the layout files changed by hand do too.
//
// What is known of the objects (US v208, report 82, lab/find.txt): a built layout keeps a pointer to its name,
// the string that follows the `lyt1` header of its file in memory (0x1C bytes after the letters); the two floats
// before that pointer are the layout's size and the word 0x10 before it is its root pane. A pane: +0x00 the next
// and the last pane beside it, +0x08 its vtable, +0x0C its parent, +0x10 the count of the panes under it, +0x14
// the first and the last of these (a list that ends at this very place), +0x44 its flag byte (bit 0: visible),
// +0x45 its alpha, +0x80 its name (24 letters).
//
// The search runs once, on a thread of its own, when Minecraft first has the player: two passes over the game's
// memory, about three seconds. At the game's input point every found pane is checked (vtable and name as found)
// and its bit cleared; when Minecraft gives the player back, or lab/hud-hide.txt holds 0, the bits are set again
// as they were found.
namespace hudHide {
struct Target {const char* layout;const char* pane;};
// The pane named here and every pane under it. Skip_00 (the button that skips a scene) stays.
static const Target kTargets[]{{"DoCommand_00","RootPane"},{"SousaGuide_00","RootPane"},{"MainShortCut_00","N_MainAll_00"}};
constexpr size_t kTargetCount=sizeof(kTargets)/sizeof(kTargets[0]);
struct Pane {uint32_t at=0,vtable=0;char name[25]{};bool held=false,shown=false;};
static std::mutex mutex;static std::vector<Pane> panes;
static std::atomic<int> mode{1},tries{0};static std::atomic<bool> scanning{false},found{false},lost{false};static std::atomic<uint64_t> nextScan{0};

static bool read(uint32_t p,void* out,size_t n){
    auto base=static_cast<uint8_t*>(getGuest());SIZE_T copied=0;
    return base&&p>=0x10000000&&uint64_t(p)+n<=0xA0000000ull&&ReadProcessMemory(GetCurrentProcess(),base+p,out,n,&copied)&&copied==n;
}
static uint32_t word(uint32_t p){uint8_t b[4];return read(p,b,4)?nativeDamage::be(b):0;}
static bool plain(uint32_t p){return p>=0x10000000&&p<0xA0000000u&&!(p&3);}
static bool nameIs(uint32_t pane,const char* name){char got[25]{};return read(pane+0x80,got,24)&&!strncmp(got,name,24);}
static uint32_t named(uint32_t pane,const char* name,int depth){
    if(depth>24||!plain(pane))return 0;
    if(nameIs(pane,name))return pane;
    uint32_t end=pane+0x14,count=word(pane+0x10),node=word(end);
    for(uint32_t i=0;i<count&&i<256&&plain(node)&&node!=end;i++,node=word(node))if(uint32_t at=named(node,name,depth+1))return at;
    return 0;
}
static void under(uint32_t pane,std::vector<Pane>& out,int depth){
    if(depth>24||out.size()>=2048||!plain(pane))return;
    Pane p;p.at=pane;p.vtable=word(pane+8);
    if(p.vtable<0x10000000||p.vtable>=0x14000000||!read(pane+0x80,p.name,24)||!p.name[0])return;
    for(auto& o:out)if(o.at==pane)return;
    out.push_back(p);
    uint32_t end=pane+0x14,count=word(pane+0x10),node=word(end);
    for(uint32_t i=0;i<count&&i<256&&plain(node)&&node!=end;i++,node=word(node))under(node,out,depth+1);
}
static DWORD WINAPI scan(void*){
    SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_BELOW_NORMAL);
    auto began=GetTickCount64();constexpr uint32_t kStep=1u<<20,kEdge=64;std::vector<uint8_t> chunk(kStep+2*kEdge);
    // Each megabyte with a little of what stands before and after it. `with` gets where chunk[0] stands and how many bytes there are.
    auto each=[&](auto&& with){
        for(uint64_t at=0x10000000;at<0xA0000000ull;at+=kStep){
            if(read(uint32_t(at-kEdge),chunk.data(),chunk.size()))with(uint32_t(at-kEdge),chunk.size(),size_t(kEdge),size_t(kEdge+kStep));
            else if(read(uint32_t(at),chunk.data(),kStep))with(uint32_t(at),size_t(kStep),size_t(0),size_t(kStep));
        }
    };
    // First pass: where each layout's name stands after a `lyt1` header.
    std::vector<std::pair<uint32_t,size_t>> names;
    each([&](uint32_t origin,size_t n,size_t from,size_t to){
        for(size_t t=0;t<kTargetCount;t++){
            const char* s=kTargets[t].layout;size_t len=strlen(s)+1;
            for(size_t i=std::max<size_t>(from,0x1C);i<to&&i+len<=n&&names.size()<64;){
                auto p=static_cast<const uint8_t*>(memchr(&chunk[i],s[0],to-i));if(!p)break;
                i=size_t(p-chunk.data());
                if(i+len<=n&&!memcmp(p,s,len)&&!memcmp(p-0x1C,"lyt1",4))names.push_back({uint32_t(origin+i),t});
                ++i;
            }
        }
    });
    // Second pass: the layouts built from them, by the pointer to the name.
    std::vector<Pane> all;size_t layouts[kTargetCount]{},top[kTargetCount]{};
    if(!names.empty()){
        uint32_t lo=~0u,hi=0;for(auto& f:names){lo=std::min(lo,f.first);hi=std::max(hi,f.first);}
        std::vector<std::pair<uint32_t,size_t>> refs;
        each([&](uint32_t origin,size_t,size_t from,size_t to){
            for(size_t i=(from+3)&~size_t(3);i+4<=to&&refs.size()<256;i+=4){
                uint32_t v=nativeDamage::be(&chunk[i]);if(v<lo||v>hi)continue;
                for(auto& f:names)if(f.first==v){refs.push_back({uint32_t(origin+i),f.second});break;}
            }
        });
        for(auto [at,t]:refs){
            uint8_t raw[8];if(!read(at-8,raw,8))continue;
            float size[2];for(int k=0;k<2;k++){uint32_t v=nativeDamage::be(raw+k*4);memcpy(&size[k],&v,4);}
            if(!(size[0]>=100&&size[0]<=10000&&size[1]>=100&&size[1]<=10000))continue;
            uint32_t root=word(at-0x10);if(!plain(root)||!nameIs(root,"RootPane"))continue;
            ++layouts[t];
            if(uint32_t pane=named(root,kTargets[t].pane,0)){++top[t];under(pane,all,0);}
        }
    }
    bool every=true;for(size_t t=0;t<kTargetCount;t++)every&=top[t]>0;
    char detail[256];size_t used=0;detail[0]=0;
    for(size_t t=0;t<kTargetCount;t++)used+=size_t(snprintf(detail+used,sizeof(detail)-used,"%s{\"layout\":\"%s\",\"built\":%zu,\"hidden\":%zu}",t?",":"",kTargets[t].layout,layouts[t],top[t]));
    event("{\"event\":\"hud_hide\",\"state\":\"%s\",\"names\":%zu,\"panes\":%zu,\"try\":%d,\"ms\":%llu,\"layouts\":[%s]}",every?"found":"partly",names.size(),all.size(),tries.load(),(unsigned long long)(GetTickCount64()-began),detail);
    {std::lock_guard lock(mutex);
        // A pane that is held hidden already keeps what it was like before.
        for(auto& p:all)for(auto& o:panes)if(o.at==p.at&&o.vtable==p.vtable){p.held=o.held;p.shown=o.shown;break;}
        panes.swap(all);}
    found=every;lost=false;nextScan=GetTickCount64()+20000;scanning=false;return 0;
}
// At the game's input point.
static void apply(){
    std::unique_lock lock(mutex,std::try_to_lock);if(!lock||panes.empty())return;
    auto base=static_cast<uint8_t*>(getGuest());if(!base)return;
    bool want=mode.load()!=0&&physical::wanted.load()&&nativeRegions::enabled.load();size_t valid=0;
    for(auto& p:panes){
        uint8_t now[0x9C];if(!read(p.at,now,sizeof(now))||nativeDamage::be(now+8)!=p.vtable||memcmp(now+0x80,p.name,24))continue;
        ++valid;bool visible=now[0x44]&1;
        if(want){
            if(!p.held){p.held=true;p.shown=visible;}
            if(visible)base[p.at+0x44]=uint8_t(now[0x44]&~1);
        }else if(p.held){
            p.held=false;if(p.shown&&!visible)base[p.at+0x44]=uint8_t(now[0x44]|1);
        }
    }
    if(!valid)lost=true;
}
// On the worker.
static void poll(){
    static uint64_t checked=0;uint64_t now=GetTickCount64();if(now-checked<500)return;checked=now;
    {int value=1;std::ifstream(directory+"/hud-hide.txt")>>value;mode=value;}
    if(lost.exchange(false)){
        std::lock_guard lock(mutex);panes.clear();found=false;tries=0;
        event("{\"event\":\"hud_hide\",\"state\":\"lost\"}");
    }
    if(!mode.load()||!physicalActive.load()||found.load()||scanning.load()||now<nextScan.load()||tries.load()>=6)return;
    scanning=true;++tries;
    if(HANDLE thread=CreateThread(nullptr,0,scan,nullptr,0,nullptr))CloseHandle(thread);else scanning=false;
}
}
