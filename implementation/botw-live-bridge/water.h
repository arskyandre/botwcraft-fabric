// BOTW's water around the player, published to Minecraft as SkyCraft's WaterGrid (report 54):
// the height of the water surface over each of the 16 x 16 block columns about the player.
// The mod already treats that as Minecraft water for entities (BotwWater: swimming, floating,
// drowning) wherever Minecraft has air below the surface.
//
// Source of the heights: the game's own question "how high is the water at this x, z",
// 0369D144(float* height, const float xz[2], reader). It has 16 callers; the player's own
// (02D65C7C) passes the reader at *0x1047C3D8 + 0x110, and the water heightfield of the physics
// world (vtable 0x10322224, layer 6) asks it the same way (03818A88) with the reader of its
// shape. It answers 0 where its data is not loaded and -1 or a kind of water otherwise.
//
// The question is asked on the game's thread that updates the player's damage manager (pack,
// ActorHit, 02AEDC50: once a frame, where the game's own thread already works on the player),
// a number of columns each frame (all 256 by default: route-113 measured 0.13 to 0.21 ms for
// them); nothing waits. The worker publishes a finished pass.
//
// The game's placed water (BoxWater actors, a box each, turned about the vertical) is not in
// that answer. The worker adds it from the actor walk (actors::waterBoxes): a column whose middle
// lies in a box's footprint has the box's top face as its surface, if that is the higher one.
// A box whose underside is above the player's head is left out, so that he is dry under it.
// lab/water.txt, fourth value: 0 leaves the boxes out.
// lab/water.txt: <on> <columns a frame> <census ms>.
#pragma once
namespace water {
namespace p=botwcraft::proto;
constexpr uint32_t kSingleton=0x1047C3D8,kCells=p::kWaterGridSize*p::kWaterGridSize;
static std::atomic<bool> enabled{true},withBoxes{true};static std::atomic<uint32_t> perFrame{256},boxed{0};static std::atomic<uint64_t> censusMs{5000};
// The pass in work: the game's thread only.
struct Pass {int32_t originX=0,originZ=0;uint32_t reader=0,cursor=0,frames=0;float height[kCells]{};int32_t code[kCells]{};int64_t ticks=0;};
static Pass work;static uint32_t frameCount=0;static int64_t frameStart=0;
// The last finished pass: handed over under a flag that neither side waits for.
static Pass done;static std::atomic<bool> handing{false};static std::atomic<uint32_t> finished{0};static std::atomic<int64_t> frameTicksMost{0};
static float botw(double minecraft,int axis){return float(nativeRegions::anchor[axis]+(minecraft-nativeRegions::origin[axis])*nativeRegions::scale);}
static double minecraft(float value,int axis){return (value-nativeRegions::anchor[axis])/nativeRegions::scale+nativeRegions::origin[axis];}
// The update of a damage manager (r3). True for the player's while a pass has columns to ask.
static bool due(CPU* c){
    if(!enabled.load(std::memory_order_relaxed))return false;
    uint32_t actor=0,player=body::identityActor.load();
    if(!player||!actors::word(c->gpr[3]+8,actor)||actor!=player)return false;
    if(work.cursor>=kCells){
        // Finished: hand it over, then begin the next one about where the player is now.
        if(handing.exchange(true))return false;
        done=work;handing=false;++finished;work.cursor=0;
    }
    if(!work.cursor){
        float at[3];uint32_t singleton=0,reader=0;
        if(!actors::playerAt(at)||!actors::word(kSingleton,singleton)||!actors::word(singleton+0x110,reader)||reader<0x10000000)return false;
        work.originX=int32_t(std::floor(minecraft(at[0],0)))-int32_t(p::kWaterGridSize/2);
        work.originZ=int32_t(std::floor(minecraft(at[2],2)))-int32_t(p::kWaterGridSize/2);
        work.reader=reader;work.frames=0;work.ticks=0;
    }
    frameCount=0;++work.frames;LARGE_INTEGER now;QueryPerformanceCounter(&now);frameStart=now.QuadPart;
    return true;
}
static void close(){
    LARGE_INTEGER now;QueryPerformanceCounter(&now);int64_t took=now.QuadPart-frameStart;work.ticks+=took;
    if(took>frameTicksMost.load(std::memory_order_relaxed))frameTicksMost=took;
}
// Bridge_WaterNext: r3 = the reader, with the column's x, z at sp+0x14 and the height's place at sp+0x10; 0: enough for this frame.
static void next(CPU* c){
    c->ip=c->spr.lr;c->gpr[3]=0;auto base=static_cast<uint8_t*>(getGuest());uint32_t sp=c->gpr[1];
    if(!base||sp<0x01000000||sp>=0xA0000000-0x40||work.cursor>=kCells||frameCount>=perFrame.load(std::memory_order_relaxed)){close();return;}
    float xz[2]{botw(work.originX+int32_t(work.cursor%p::kWaterGridSize)+.5,0),botw(work.originZ+int32_t(work.cursor/p::kWaterGridSize)+.5,2)};
    uint32_t words[3]{0,0,0};memcpy(&words[1],&xz[0],4);memcpy(&words[2],&xz[1],4);
    for(int i=0;i<3;i++){uint32_t v=__builtin_bswap32(words[i]);memcpy(base+sp+0x10+i*4,&v,4);}
    c->gpr[3]=work.reader;
}
// Bridge_WaterResult: r3 = the game's answer, the height at sp+0x10.
static void result(CPU* c){
    c->ip=c->spr.lr;auto base=static_cast<uint8_t*>(getGuest());uint32_t sp=c->gpr[1];
    if(!base||sp<0x01000000||sp>=0xA0000000-0x40||work.cursor>=kCells)return;
    uint32_t v;memcpy(&v,base+sp+0x10,4);v=__builtin_bswap32(v);float h;memcpy(&h,&v,4);
    work.code[work.cursor]=int32_t(c->gpr[3]);work.height[work.cursor]=h;++work.cursor;++frameCount;
}
// The update of every damage manager comes through here: a blow for this actor (actors::take, r12 = 1), or the player's turn
// to ask the game for objects (spawn.h, r12 = 3; rare, and before the water, which is asked about every frame) or about water (r12 = 2).
static void managerUpdate(CPU* c){
    actors::take(c);
    if(!c->gpr[12]&&spawn::due(c))c->gpr[12]=3;
    if(!c->gpr[12]&&due(c))c->gpr[12]=2;
}
static bool wet(int32_t code,float height){return code!=0&&code!=-1&&std::isfinite(height)&&std::abs(height)<100000;}
static void publish(const Pass* pass){
    auto grid=live::at<p::WaterGrid>(p::kOffWaterGrid);auto seq=live::atom(grid->seq);auto value=seq.load(std::memory_order_relaxed);
    seq.store(value+1,std::memory_order_relaxed);std::atomic_thread_fence(std::memory_order_release);
    grid->originX=pass?pass->originX:0;grid->originZ=pass?pass->originZ:0;grid->worldId=1;
    float player[3]{};bool placed=pass&&withBoxes.load()&&!actors::waterBoxes.empty()&&actors::playerAt(player);uint32_t inBoxes=0;
    for(uint32_t i=0;i<kCells;i++){
        float surface=pass&&wet(pass->code[i],pass->height[i])?pass->height[i]:-1e30f;
        if(placed){
            float x=botw(pass->originX+int32_t(i%p::kWaterGridSize)+.5,0),z=botw(pass->originZ+int32_t(i/p::kWaterGridSize)+.5,2);bool in=false;
            for(auto& w:actors::waterBoxes){
                float dx=x-w.top[0],dz=z-w.top[2],lx=dx*w.ax[0]+dz*w.ax[1],lz=dx*w.az[0]+dz*w.az[1];
                if(std::abs(lx)>w.size[0]*.5f||std::abs(lz)>w.size[2]*.5f||w.top[1]-w.size[1]>player[1]+2.5f)continue;
                in=true;surface=std::max(surface,w.top[1]);
            }
            inBoxes+=in;
        }
        grid->surface[i]=surface>-1e29f?float(minecraft(surface,1)):p::kNoWater;
    }
    boxed=inBoxes;
    seq.store(value+2,std::memory_order_release);
}
static void census(const Pass& pass,uint32_t passes){
    static FILE* file=fopen((directory+"/water.jsonl").c_str(),"a");if(!file)return;
    FILETIME now;GetSystemTimeAsFileTime(&now);uint64_t ms=(((uint64_t(now.dwHighDateTime)<<32)|now.dwLowDateTime)-116444736000000000ULL)/10000;
    fprintf(file,"{\"unix_ms\":%llu,\"passes\":%u,\"origin\":[%d,%d],\"reader\":%u,\"frames\":%u,\"code\":[",(unsigned long long)ms,passes,pass.originX,pass.originZ,pass.reader,pass.frames);
    for(uint32_t i=0;i<kCells;i++)fprintf(file,"%s%d",i?",":"",pass.code[i]);
    fputs("],\"botw_height\":[",file);
    for(uint32_t i=0;i<kCells;i++)fprintf(file,"%s%.5g",i?",":"",std::isfinite(pass.height[i])?pass.height[i]:-1e30f);
    fputs("]}\n",file);fflush(file);
}
static void poll(){
    static uint64_t checked=0,censusAt=0;static uint32_t seen=0;static bool cleared=true;auto now=GetTickCount64();
    if(now-checked>=1000){checked=now;int on=1,boxes=1;uint32_t each=256;uint64_t every=5000;std::ifstream(directory+"/water.txt")>>on>>each>>every>>boxes;enabled=on!=0;withBoxes=boxes!=0;perFrame=std::clamp<uint32_t>(each,1,kCells);censusMs=std::clamp<uint64_t>(every,100,600000);}
    if(!live::base||!live::connected||live::failed)return;
    if(!enabled.load()||!body::allowed()){if(!cleared){publish(nullptr);cleared=true;}return;}
    uint32_t count=finished.load();if(count==seen||handing.exchange(true))return;
    static Pass copy;copy=done;handing=false;seen=count;publish(&copy);cleared=false;
    if(now-censusAt>=censusMs.load()){
        censusAt=now;census(copy,count);LARGE_INTEGER f;QueryPerformanceFrequency(&f);
        uint32_t wetCells=0;float low=1e30f,high=-1e30f;int32_t kinds[4]{0,0,0,0};
        for(uint32_t i=0;i<kCells;i++){
            if(wet(copy.code[i],copy.height[i])){++wetCells;low=std::min(low,copy.height[i]);high=std::max(high,copy.height[i]);}
            ++kinds[copy.code[i]==0?0:copy.code[i]==-1?1:copy.code[i]==1?2:3];
        }
        event("{\"event\":\"water_grid\",\"boxes\":%u,\"columns_in_boxes\":%u,\"passes\":%u,\"origin\":[%d,%d],\"wet\":%u,\"unloaded\":%d,\"none\":%d,\"kind_1\":%d,\"kind_other\":%d,\"lowest\":%.5g,\"highest\":%.5g,\"frames\":%u,\"pass_ms\":%.4f,\"frame_ms_most\":%.4f}",
            uint32_t(actors::waterBoxes.size()),boxed.load(),count,copy.originX,copy.originZ,wetCells,kinds[0],kinds[1],kinds[2],kinds[3],wetCells?low:0.f,wetCells?high:0.f,copy.frames,double(copy.ticks)*1000/double(f.QuadPart),double(frameTicksMost.exchange(0))*1000/double(f.QuadPart));
    }
}
}
