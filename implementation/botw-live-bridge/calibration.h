// Bounded diagnostic only. Native query sequence: BetterVR 2e06b31 (MIT).
// No actor writes, host Havok traversal, or general terrain extractor.
#pragma once
namespace calibration {
struct Ray {uint64_t id=0,epoch=0,until=0;float from[3]{},delta[3]{};};
static std::atomic<std::shared_ptr<const Ray>> request;
static std::atomic<uint64_t> claimed{0};
// A claimed ray that was not cast after all (the player was no longer near its start).
static std::atomic<uint64_t> unused{0};
static thread_local Ray running;
static thread_local uint64_t begin=0;
// Latest native hit, handed to the worker (ground_check compares it with the
// exported mesh). Immutable publication; the game thread never waits.
struct Hit {uint64_t id=0,epoch=0;bool hit=false;float from[3]{},delta[3]{},point[3]{};};
static std::atomic<std::shared_ptr<const Hit>> lastHit;
static void prepare(CPU* c){
    body::ret(c);auto scratch=c->gpr[3],ctrl=c->gpr[4];c->gpr[3]=0;
    auto ray=request.load();if(!ray||ray->id==claimed.load()||!body::allowed()||
        ray->epoch!=body::lifecycleEpoch.load()||GetTickCount64()>ray->until||
        ctrl!=body::identityCtrl.load()||!body::readable(scratch,40,0x10000))return;
    uint64_t old=claimed.load();if(old==ray->id||!claimed.compare_exchange_strong(old,ray->id))return;
    auto actor=body::player.load();if(!body::readable(actor,0x234)){unused=ray->id;return;}
    float distance=0;for(int i=0;i<3;i++)distance+=std::pow(ray->from[i]-body::f32(actor+0x204+i*16),2);
    if(distance>32*32){unused=ray->id;return;}
    running=*ray;
    for(int i=0;i<3;i++){body::putf(scratch+4*i,ray->from[i]);body::putf(scratch+12+4*i,ray->delta[i]);body::putf(scratch+24+4*i,0);}
    body::put32(scratch+36,15); // BetterVR kRoomscaleGroundHit
    LARGE_INTEGER q;QueryPerformanceCounter(&q);begin=q.QuadPart;c->gpr[3]=1;
}
static void consume(CPU* c){
    body::ret(c);auto scratch=c->gpr[3];bool hit=c->gpr[4]!=0;
    if(!begin||!body::readable(scratch,40,0x10000))return;
    LARGE_INTEGER q;QueryPerformanceCounter(&q);
    event("{\"event\":\"calibration_ray\",\"id\":%llu,\"epoch\":%llu,\"current_epoch\":%llu,\"qpc\":%llu,\"ticks\":%llu,\"body_seq\":%llu,\"hit\":%d,\"from\":[%.9g,%.9g,%.9g],\"delta\":[%.9g,%.9g,%.9g],\"point\":[%.9g,%.9g,%.9g]}",running.id,running.epoch,body::lifecycleEpoch.load(),(uint64_t)q.QuadPart,uint64_t(q.QuadPart)-begin,body::controlEpoch.load(),int(hit),running.from[0],running.from[1],running.from[2],running.delta[0],running.delta[1],running.delta[2],body::f32(scratch+24),body::f32(scratch+28),body::f32(scratch+32));begin=0;
    Hit h;h.id=running.id;h.epoch=running.epoch;h.hit=hit;std::copy_n(running.from,3,h.from);std::copy_n(running.delta,3,h.delta);
    for(int i=0;i<3;i++)h.point[i]=body::f32(scratch+24+4*i);
    lastHit.store(std::make_shared<const Hit>(h));
}
static void poll(){
    if(nativeRegions::enabled.load())return; // Do not replace an in-flight stream job with an old manual ray.
    std::ifstream file(directory+"/calibration-ray.txt");Ray ray;bool ok=bool(file>>ray.id>>ray.epoch);
    for(float& v:ray.from)ok&=bool(file>>v)&&std::isfinite(v)&&std::abs(v)<100000;
    float length=0;for(float& v:ray.delta){ok&=bool(file>>v)&&std::isfinite(v);length+=v*v;}
    std::string extra;ok&=!(file>>extra);ok&=length>0&&length<=32*32;
    auto prior=request.load();if(ok&&ray.id&&(!prior||prior->id!=ray.id)){
        ray.until=GetTickCount64()+1000;request.store(std::make_shared<const Ray>(ray));
    }
}
}
