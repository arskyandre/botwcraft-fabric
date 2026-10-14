// Portable v11 ownership/seqlock/rings adapted from SkyCraft Link.cpp (MIT).
// This stage observes real IPC only. No native body/camera authority or terrain
// is advertised. The parked MC origin below is NOT a calibrated BOTW transform.
#pragma once
#include "../botwcraft-fabric/protocol/botwcraft_protocol.h"
namespace walls {static void section(const botwcraft::proto::RenSolids&,const uint8_t* bits);static void clear();}
namespace live {
namespace p=botwcraft::proto;
static_assert(p::kVersion==22 && p::kMappingBytes==200327168);
static_assert(sizeof(p::McState)==0xD8 && sizeof(p::RenVertex)==32);
template<class T> static auto atom(T& x){return std::atomic_ref<T>(x);}
static HANDLE mapping=nullptr;
static uint8_t* base=nullptr;
static uint32_t front=1,atlasW=0,atlasH=0,peer=0,entitySeq=0;
static bool failed=false,enabledBefore=false,connected=false,peerHasWorld=false;
static uint64_t messages[12]{},bytes=0,overlayFrames=0,vertices=0,lastSample=0;
static uint64_t sectionRemovals=0,clears=0,inputs=0,session=0;
static p::McState mc{};
static float commonYaw=0,commonPitch=0;
static uint32_t overlayW=1280,overlayH=720;
static void commonView(){

    // Diagnostic host look publishes the common angles directly; no Java
    // round trip per delta. Physical focused input remains a later gate.
    static uint64_t lookToken=0;uint64_t token;float yaw,pitch;
    std::ifstream look(directory+"/common-look.txt");
    if(look>>token>>yaw>>pitch&&token!=lookToken&&std::isfinite(yaw)&&std::isfinite(pitch)&&std::abs(yaw)<100000&&std::abs(pitch)<=89){lookToken=token;commonYaw=yaw;commonPitch=pitch;}
    static uint64_t last=0,expires=0;static float origin[3]{},anchor[3]{},scale=0;
    static ViewProbe lease;
    unsigned duration;uint64_t bodyEpoch,targetEpochRequested;uint32_t actorId;
    float nextScale,nextOrigin[3],nextAnchor[3];
    std::ifstream file(directory+"/common-view.txt");
    if(file>>token>>nextScale>>nextOrigin[0]>>nextOrigin[1]>>nextOrigin[2]>>nextAnchor[0]>>nextAnchor[1]>>nextAnchor[2]>>duration>>bodyEpoch>>targetEpochRequested>>actorId&&token!=last){
        last=token;bool valid=duration<=750&&nextScale>0&&nextScale<10&&bodyEpoch==body::lifecycleEpoch.load()&&targetEpochRequested==targetEpoch.load();
        for(float v:{nextScale,nextOrigin[0],nextOrigin[1],nextOrigin[2],nextAnchor[0],nextAnchor[1],nextAnchor[2]})valid&=std::isfinite(v)&&std::abs(v)<100000;
        if(valid){scale=nextScale;std::copy_n(nextOrigin,3,origin);std::copy_n(nextAnchor,3,anchor);}
        lease={};lease.id=token;lease.epoch=bodyEpoch;lease.target=targetEpochRequested;lease.peerSession=session;lease.actor=body::player.load();lease.procId=actorId;lease.common=true;lease.scale=nextScale;std::copy_n(nextOrigin,3,lease.origin);std::copy_n(nextAnchor,3,lease.anchor);
        expires=GetTickCount64()+(valid?duration:0);
        if(valid&&duration)physicalSeed.store(std::make_shared<ViewProbe>(lease));
    }
    auto now=GetTickCount64();
    if(lease.id&&now<expires&&now-peerViewAt.load()<std::max<uint64_t>(500,physicalActive.load()?staleHoldMs.load():0)&&body::allowed()&&lease.epoch==body::lifecycleEpoch.load()&&lease.target==targetEpoch.load()){
        lease.until=expires;lease.yaw=commonYaw;lease.pitch=commonPitch;lease.fov=mc.fovDeg;lease.aspect=float(overlayW)/overlayH;lease.mcSeq=mc.seq;
        double eye[]={mc.eyeX,mc.eyeY,mc.eyeZ};for(int i=0;i<3;i++)lease.eye[i]=anchor[i]+float(eye[i]-origin[i])*scale;
        bool valid=lease.fov>=20&&lease.fov<=120&&(mc.flags&p::kMcInWorld);
        if(valid&&!physicalActive.load()&&!physicalBlocked.load()&&!viewProbeBusy.test_and_set(std::memory_order_acquire)){viewProbe=lease;viewProbeBusy.clear(std::memory_order_release);}
    }else if(lease.id){
        lease.id=0;expires=0;
        if(!viewProbeBusy.test_and_set(std::memory_order_acquire)){viewProbe.until=0;viewProbeBusy.clear(std::memory_order_release);}
        event("{\"event\":\"common_view_release\",\"body_epoch\":%llu,\"target_epoch\":%llu,\"peer_age_ms\":%llu}",body::lifecycleEpoch.load(),targetEpoch.load(),now-peerViewAt.load());
    }
}
template<class T> static T* at(uint64_t offset){return reinterpret_cast<T*>(base+offset);}
static void close(){
    physicalActive=false;physicalBlocked=true;ClipCursor(nullptr);
    while(botwWriteBusy.test_and_set(std::memory_order_acquire))SwitchToThread();
    if(base){atom(at<p::Header>(0)->magic).store(0,std::memory_order_release);UnmapViewOfFile(base);}
    if(mapping)CloseHandle(mapping);
    base=nullptr;mapping=nullptr;connected=false;presentation::reset(session);botwWriteBusy.clear(std::memory_order_release);
}
static bool create(FILE* log){
    auto handle=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,DWORD(p::kMappingBytes),L"Local\\BotwCraft_v1");
    DWORD error=GetLastError();
    if(!handle||error==ERROR_ALREADY_EXISTS){if(handle)CloseHandle(handle);fprintf(log,"IPC create refused error=%lu (existing mappings require a fresh peer session)\n",error);return false;}
    auto view=static_cast<uint8_t*>(MapViewOfFile(handle,FILE_MAP_ALL_ACCESS,0,0,p::kMappingBytes));
    if(!view){fprintf(log,"IPC map size=%llu failed error=%lu\n",p::kMappingBytes,GetLastError());CloseHandle(handle);return false;}
    // Fresh pagefile mapping is zero initialized, including all ring indices.
    mapping=handle;base=view;front=1;peer=0;entitySeq=0;atlasW=atlasH=0;failed=false;peerHasWorld=false;
    memset(messages,0,sizeof(messages));bytes=overlayFrames=vertices=lastSample=0;
    sectionRemovals=clears=inputs=0;mc={};++session;
    presentation::reset(session);
    auto h=at<p::Header>(0);h->version=p::kVersion;h->botwPid=GetCurrentProcessId();
    atom(h->botwHeartbeatMs).store(GetTickCount64(),std::memory_order_release);
    atom(h->magic).store(p::kMagic,std::memory_order_release);
    event("{\"event\":\"ipc_create\",\"session\":%llu,\"pid\":%lu,\"version\":%u,\"bytes\":%llu}",session,GetCurrentProcessId(),p::kVersion,p::kMappingBytes);
    return true;
}
template<class T> static bool seqlock(T* src,T& out){
    for(int i=0;i<3;i++){
        auto first=atom(src->seq).load(std::memory_order_acquire);if(first&1)continue;
        memcpy(&out,src,sizeof(T));std::atomic_thread_fence(std::memory_order_acquire);
        if(first==atom(src->seq).load(std::memory_order_relaxed))return true;
    }return false;
}
static bool finiteState(const p::McState& s){
    for(double v:{s.x,s.y,s.z,s.eyeX,s.eyeY,s.eyeZ,s.prevX,s.prevY,s.prevZ,s.curX,s.curY,s.curZ})if(!std::isfinite(v)||std::abs(v)>3e7)return false;
    for(float v:{s.yaw,s.pitch,s.eyeHeight,s.sensitivity,s.fovDeg,s.tickEyeO,s.tickEye,s.tickMs,s.absorption})if(!std::isfinite(v))return false;
    return s.cameraMode<=2 && s.sensitivity>=0&&s.sensitivity<=1&&
        (!(s.flags&p::kMcInWorld)||(s.tickMs>0&&s.tickMs<=1000));
}
static bool push(const p::InputEvent& input){
    if(!base||!connected||failed)return false;
    auto ring=base+p::kOffInputRing;auto& h=*reinterpret_cast<uint64_t*>(ring);auto& t=*reinterpret_cast<uint64_t*>(ring+0x40);
    auto head=atom(h).load(std::memory_order_relaxed),tail=atom(t).load(std::memory_order_acquire);
    if(head<tail||head-tail>=p::kInputRingEntries)return false;
    auto dst=reinterpret_cast<p::InputEvent*>(ring+p::kInputRingDataOff)+(head&(p::kInputRingEntries-1));
    *dst=input;atom(h).store(head+1,std::memory_order_release);++inputs;
    LARGE_INTEGER q;QueryPerformanceCounter(&q);
    event("{\"event\":\"ipc_input\",\"qpc\":%llu,\"session\":%llu,\"head\":%llu,\"type\":%u,\"code\":%u,\"a\":%d,\"b\":%d}",(uint64_t)q.QuadPart,session,head,input.type,input.code,input.a,input.b);
    return true;
}
static bool mesh(const uint8_t* data,uint32_t n,uint32_t header,uint32_t batches,uint32_t count){
    uint64_t start=uint64_t(header)+uint64_t(batches)*sizeof(p::RenBatch);
    if(count%3||start+uint64_t(count)*sizeof(p::RenVertex)!=n)return false;
    for(uint32_t i=0;i<batches;i++){
        p::RenBatch b;memcpy(&b,data+header+i*sizeof(b),sizeof(b));
        if(b.count%3||uint64_t(b.first)+b.count>count)return false;
    }
    for(uint32_t i=0;i<count;i++){
        p::RenVertex v;memcpy(&v,data+start+i*sizeof(v),sizeof(v));
        for(float f:{v.x,v.y,v.z,v.u,v.v})if(!std::isfinite(f))return false;
        if(v.u<0||v.u>1||v.v<0||v.v>1||(v.light&255)>15||((v.light>>8)&255)>15)return false;
    }vertices+=count;return true;
}
template<class T> static bool header(const uint8_t* data,uint32_t n,T& value){if(n<sizeof(T))return false;memcpy(&value,data,sizeof(T));return true;}
static bool payload(uint32_t type,const uint8_t* data,uint32_t n){
    switch(type){
    case p::kRenAtlas:{p::RenAtlas a;if(!header(data,n,a)||!a.width||!a.height||a.width>8192||a.height>8192||sizeof(a)+uint64_t(a.width)*a.height*4!=n)return false;atlasW=a.width;atlasH=a.height;return true;}
    case p::kRenTexture:{p::RenTexture a;return header(data,n,a)&&a.id&&a.width&&a.height&&a.width<=8192&&a.height<=8192&&sizeof(a)+uint64_t(a.width)*a.height*4==n;}
    case p::kRenAtlasRegion:{p::RenAtlasRegion a;return header(data,n,a)&&a.width&&a.height&&uint64_t(a.x)+a.width<=atlasW&&uint64_t(a.y)+a.height<=atlasH&&sizeof(a)+uint64_t(a.width)*a.height*4==n;}
    case p::kRenSection:{p::RenSection a;if(!header(data,n,a)||!mesh(data,n,sizeof(a),0,a.vertexCount))return false;if(!a.vertexCount)++sectionRemovals;return true;}
    case p::kRenScene:{p::RenScene a;return header(data,n,a)&&std::isfinite(a.originX)&&std::isfinite(a.originY)&&std::isfinite(a.originZ)&&mesh(data,n,sizeof(a),a.batchCount,a.vertexCount);}
    case p::kRenAvatar:case p::kRenRagdoll:{p::RenAvatar a;return header(data,n,a)&&mesh(data,n,sizeof(a),a.batchCount,a.vertexCount);}
    case p::kRenLights:{p::RenLights a;return header(data,n,a)&&a.count<=4096&&sizeof(a)+uint64_t(a.count)*sizeof(p::RenLight)==n;}
    case p::kRenSolids:{p::RenSolids a;if(!header(data,n,a)||a.count>4096||n!=sizeof(a)+(a.count?512:0))return false;walls::section(a,data+sizeof(a));return true;}
    case p::kRenDug:{p::RenDug a;return header(data,n,a)&&a.count<=4096&&n==sizeof(a)+(a.count?512:0);}
    case p::kRenClearAll:if(n)return false;atlasW=atlasH=0;++clears;++trace::renderClears;walls::clear();return true;
    default:return false;
    }
}
static void fault(const char* reason){
    // Release while the mapping is still valid, then latch until an off/on edge.
    push({p::kInReleaseAll,0,0,0,0});physicalBlocked=true;presentation::reset(session);failed=true;connected=false;
    // Worker only: finish an already frozen frame publication before changing
    // the same seqlock. The render callback never waits for this claim.
    while(botwWriteBusy.test_and_set(std::memory_order_acquire))SwitchToThread();
    auto state=at<p::BotwState>(p::kOffBotwState);auto seq=atom(state->seq);auto value=seq.load(std::memory_order_relaxed);
    seq.store(value+1,std::memory_order_relaxed);std::atomic_thread_fence(std::memory_order_release);
    state->flags=p::kBotwMenuOpen;seq.store(value+2,std::memory_order_release);
    botwWriteBusy.clear(std::memory_order_release);
    event("{\"event\":\"ipc_fault\",\"session\":%llu,\"reason\":\"%s\"}",session,reason);
}
static void drain(FILE* log){
    auto ring=base+p::kOffRenderRing;auto& h=*reinterpret_cast<uint64_t*>(ring);auto& t=*reinterpret_cast<uint64_t*>(ring+0x40);
    uint64_t head=atom(h).load(std::memory_order_acquire),tail=atom(t).load(std::memory_order_relaxed),used=0;
    constexpr uint64_t capacity=p::kRenRingDataBytes;
    if(head<tail||head-tail>capacity){fault("render-ring-distance");return;}
    while(tail<head&&used<(4u<<20)){
        uint64_t pos=tail%capacity;if(capacity-pos<8||head-tail<8){fault("render-header");return;}
        p::ColMsgHeader hdr;memcpy(&hdr,ring+0x80+pos,8);
        if(hdr.type==p::kRenPad){if(hdr.payloadBytes||head-tail<capacity-pos){fault("render-pad");return;}tail+=capacity-pos;continue;}
        uint64_t span=(8ull+hdr.payloadBytes+7)&~7ull;
        if(hdr.type>11||span>capacity/2||span>capacity-pos||span>head-tail){fault("render-span");return;}
        auto data=ring+0x88+pos;
        if(!payload(hdr.type,data,hdr.payloadBytes)){fault("render-payload");fprintf(log,"bad render type=%u bytes=%u tail=%llu\n",hdr.type,hdr.payloadBytes,tail);return;}
        if(!presentation::accept(hdr.type,data,hdr.payloadBytes)){fault("owned-snapshot-capacity");return;}
        ++messages[hdr.type];bytes+=span;used+=span;
        if(hdr.type!=p::kRenAtlasRegion||messages[hdr.type]%128==1)event("{\"event\":\"ipc_export\",\"session\":%llu,\"tail\":%llu,\"type\":%u,\"bytes\":%u,\"sequence\":%llu}",session,tail,hdr.type,hdr.payloadBytes,messages[hdr.type]);
        // Bounded raw first payload captures on the worker only; no frame loop.
        if(messages[hdr.type]==1&&(hdr.type==p::kRenAtlas||hdr.type==p::kRenSection)){
            FILE* file=fopen((directory+"/live-type-"+std::to_string(hdr.type)+".bin").c_str(),"wb");
            if(file){fwrite(data,1,hdr.payloadBytes,file);fclose(file);}
        }
        tail+=span;atom(t).store(tail,std::memory_order_release);
    }
    atom(t).store(tail,std::memory_order_release);presentation::publish();
}
static void overlay(FILE* log){
    auto ctl=at<p::OverlayCtl>(p::kOffOverlayCtl);auto state=atom(ctl->state).load(std::memory_order_acquire);
    if((state&3)>=3){fault("overlay-slot");return;}if(!(state&p::kOverlayDirty))return;
    auto old=atom(ctl->state).exchange(front,std::memory_order_acq_rel);front=old&3;
    if(front>=3){fault("overlay-exchange");return;}
    auto hdr=*at<p::OverlaySlotHdr>(p::kOffOverlaySlotHdr+front*sizeof(p::OverlaySlotHdr));
    if(!hdr.width||!hdr.height||hdr.width>p::kMaxOverlayW||hdr.height>p::kMaxOverlayH||(hdr.flags&~1)){fault("overlay-dimensions");return;}
    overlayW=hdr.width;overlayH=hdr.height;
    presentation::overlay(session,hdr.frameId,hdr.width,hdr.height,base+p::kOffOverlayPixels+front*p::kOverlaySlotBytes);
    ++overlayFrames;
    event("{\"event\":\"ipc_overlay\",\"session\":%llu,\"sequence\":%llu,\"frame\":%llu,\"published\":%llu,\"slot\":%u,\"width\":%u,\"height\":%u,\"flags\":%u}",session,overlayFrames,hdr.frameId,atom(ctl->framesPublished).load(std::memory_order_acquire),front,hdr.width,hdr.height,hdr.flags);
    // Consumer owns front for this entire copy. Capture first and ~3s intervals,
    // at most four samples; these are actual hand/HUD bytes, not native uploads.
    static uint64_t captureSession=0,captured=0,captureAt=0;
    if(captureSession!=session){captureSession=session;captured=0;captureAt=0;}
    static std::string captureToken;std::string token;std::ifstream captureFile(directory+"/live-capture.txt");std::getline(captureFile,token);
    bool forced=!token.empty()&&token!=captureToken;if(forced)captureToken=token;
    if((mc.flags&p::kMcInWorld)&&((captured<4&&GetTickCount64()-captureAt>3000)||(forced&&captured<12))){
        captureAt=GetTickCount64();auto name=directory+"/live-overlay-"+std::to_string(++captured);
        FILE* f=fopen((name+".rgba").c_str(),"wb");
        if(f){fwrite(base+p::kOffOverlayPixels+front*p::kOverlaySlotBytes,1,size_t(hdr.width)*hdr.height*4,f);fclose(f);}
        f=fopen((name+".json").c_str(),"w");
        if(f){fprintf(f,"{\"width\":%u,\"height\":%u,\"flags\":%u,\"frame\":%llu}",hdr.width,hdr.height,hdr.flags,hdr.frameId);fclose(f);}
        fprintf(log,"IPC actual overlay captured frame=%llu slot=%u\n",hdr.frameId,front);
    }
}
static void entities(){
    p::WorldEntities value{};
    if(!seqlock(at<p::WorldEntities>(p::kOffWorldEntities),value)||value.seq==entitySeq)return;
    if(value.count>p::kMaxWorldEntities||value.hasSelection>1){fault("world-entity-count");return;}
    for(uint32_t i=0;i<value.count;i++){
        auto& e=value.entities[i];if(e.kind<p::kWeArrow||e.kind>p::kWeShadow){fault("world-entity-kind");return;}
        for(float f:{e.x,e.y,e.z,e.yaw,e.pitch,e.scale,e.ext[0],e.ext[1],e.ext[2]})if(!std::isfinite(f)){fault("world-entity-finite");return;}
        for(auto& uv:e.uv)for(float f:uv)if(!std::isfinite(f)||f<0||f>1){fault("world-entity-uv");return;}
    }
    entitySeq=value.seq;
    if(presentation::dynamic(value))presentation::publish();
    static uint32_t previousCount=~0u;static uint64_t logged=0;
    if(value.count!=previousCount||GetTickCount64()-logged>200){
        previousCount=value.count;logged=GetTickCount64();LARGE_INTEGER now;QueryPerformanceCounter(&now);
        auto e=value.entities[0];
        event("{\"event\":\"ipc_entities\",\"qpc\":%llu,\"session\":%llu,\"seq\":%u,\"count\":%u,\"selection\":%u,\"first_id\":%u,\"first_kind\":%u,\"first_pos\":[%.9g,%.9g,%.9g]}",(uint64_t)now.QuadPart,session,value.seq,value.count,value.hasSelection,e.id,e.kind,e.x,e.y,e.z);
    }
}
}
namespace actors {static void hit(const botwcraft::proto::McEvent&);}
namespace swing {static void asked(const botwcraft::proto::McEvent&);static bool aimedAt(uint32_t actor,uint32_t& heldMs);static bool ownsThings();}
namespace live {
static void events(){
    auto ring=base+p::kOffEventRing;auto& h=*reinterpret_cast<uint64_t*>(ring);auto& t=*reinterpret_cast<uint64_t*>(ring+0x40);
    auto head=atom(h).load(std::memory_order_acquire),tail=atom(t).load(std::memory_order_relaxed);
    if(head<tail||head-tail>p::kEventRingEntries){fault("mc-event-ring-distance");return;}
    while(tail<head){
        p::McEvent value;memcpy(&value,ring+0x80+(tail&(p::kEventRingEntries-1))*sizeof(value),sizeof(value));
        if(value.type<p::kEvHitActor||value.type>p::kEvSwing){fault("mc-event-type");return;}
        for(float f:{value.a,value.b,value.c,value.d})if(!std::isfinite(f)){fault("mc-event-finite");return;}
        // A blow of Minecraft's player on an actor's stand-in goes to that actor (actors.h). The rest is not acted on yet.
        if(value.type==p::kEvHitActor)actors::hit(value);
        else if(value.type==p::kEvPlayerDied){peerDeathCause.store(value.flags);peerDeathAttacker.store(value.formId);peerDiedAt.store(GetTickCount64());}   // Steve's death is Link's (physical.h, hearts())
        else if(value.type==p::kEvSwing)swing::asked(value);   // the attack click: a weapon of the game's swung there (swing.h)
        else event("{\"event\":\"ipc_ignored_event\",\"session\":%llu,\"tail\":%llu,\"type\":%u,\"form_id\":%u}",session,tail,value.type,value.formId);
        atom(t).store(++tail,std::memory_order_release);
    }
}
static void poll(FILE* log){
    std::ifstream config(directory+"/live-ipc.txt");int enable=0;config>>enable;bool requested=enable==1;
    if(!requested&&enabledBefore){if(base)push({p::kInReleaseAll,0,0,0,0});close();failed=false;}
    if(requested&&!enabledBefore&&!create(log))failed=true;
    enabledBefore=requested;if(!base){peerViewAt.store(0);return;}
    auto h=at<p::Header>(0);atom(h->botwHeartbeatMs).store(GetTickCount64(),std::memory_order_release);
    if(failed){peerViewAt.store(0);return;}
    auto pid=atom(h->mcPid).load(std::memory_order_acquire);auto beat=atom(h->mcHeartbeatMs).load(std::memory_order_acquire);
    // Initial world creation blocks Java's render heartbeat. No held input or
    // native authority exists then. Once in-world, the 3s failure is latched.
    bool alive=pid&&beat&&beat<=GetTickCount64()&&GetTickCount64()-beat<(peerHasWorld?3000:30000);
    if(connected&&(!alive||pid!=peer)){fault("peer-heartbeat-or-pid");return;}
    if(alive&&!connected){connected=true;peer=pid;event("{\"event\":\"ipc_connected\",\"session\":%llu,\"mc_pid\":%u}",session,pid);}
    walking::poll();
    p::BotwState sky{};sky.flags=body::allowed()?p::kBotwInGame:p::kBotwMenuOpen;
    commonView();if(physicalActive.load()){commonYaw=physicalYaw.load();commonPitch=physicalPitch.load();}sky.yaw=commonYaw;sky.pitch=commonPitch;
    sky.worldId=1;sky.collisionEpoch=1;sky.posY=100;sky.teleportSeq=1;sky.viewportW=std::min(hostSceneW.load(),p::kMaxOverlayW);sky.viewportH=std::min(hostSceneH.load(),p::kMaxOverlayH);sky.gameHour=12;
    walking::configure(sky);
    auto dst=at<p::BotwState>(p::kOffBotwState);
    if(!botwWriteBusy.test_and_set(std::memory_order_acquire)){auto seq=atom(dst->seq);auto s=seq.load(std::memory_order_relaxed);
    seq.store(s+1,std::memory_order_relaxed);std::atomic_thread_fence(std::memory_order_release);
    memcpy(reinterpret_cast<uint8_t*>(dst)+4,reinterpret_cast<uint8_t*>(&sky)+4,sizeof(sky)-4);seq.store(s+2,std::memory_order_release);botwWriteBusy.clear(std::memory_order_release);}
    if(!connected)return;
    p::McState current{};
    if(seqlock(at<p::McState>(p::kOffMcState),current)&&current.seq!=mc.seq){
        if(!finiteState(current)){fault("mc-state");return;}mc=current;
        if(alive&&(mc.flags&p::kMcInWorld))peerViewAt.store(GetTickCount64());
        peerHasWorld|=bool(mc.flags&p::kMcInWorld);
        if(GetTickCount64()-lastSample>50){lastSample=GetTickCount64();LARGE_INTEGER now;QueryPerformanceCounter(&now);
            event("{\"event\":\"ipc_mc\",\"qpc\":%llu,\"session\":%llu,\"seq\":%u,\"frame\":%llu,\"flags\":%u,\"ack\":%u,\"pos\":[%.12g,%.12g,%.12g],\"eye\":[%.12g,%.12g,%.12g],\"yaw\":%.9g,\"pitch\":%.9g,\"fov\":%.9g,\"sensitivity\":%.9g,\"camera_mode\":%u,\"tick_qpc\":%lld,\"tick_ms\":%.9g,\"prev\":[%.12g,%.12g,%.12g],\"cur\":[%.12g,%.12g,%.12g],\"tick_eye\":[%.9g,%.9g]}",(uint64_t)now.QuadPart,session,mc.seq,mc.frameCounter,mc.flags,mc.teleportAck,mc.x,mc.y,mc.z,mc.eyeX,mc.eyeY,mc.eyeZ,mc.yaw,mc.pitch,mc.fovDeg,mc.sensitivity,mc.cameraMode,(long long)mc.tickQpc,mc.tickMs,mc.prevX,mc.prevY,mc.prevZ,mc.curX,mc.curY,mc.curZ,mc.tickEyeO,mc.tickEye);
        }
    }
    commonView();
    drain(log);if(!failed)overlay(log);if(!failed)entities();if(!failed)events();
    static uint64_t summaryAt=0;
    if(GetTickCount64()-summaryAt>=1000){summaryAt=GetTickCount64();LARGE_INTEGER now;QueryPerformanceCounter(&now);
        auto input=base+p::kOffInputRing;
        event("{\"event\":\"ipc_counters\",\"qpc\":%llu,\"session\":%llu,\"bytes\":%llu,\"vertices\":%llu,\"overlays\":%llu,\"input_head\":%llu,\"input_tail\":%llu,\"render_types\":[%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu]}",(uint64_t)now.QuadPart,session,bytes,vertices,overlayFrames,atom(*reinterpret_cast<uint64_t*>(input)).load(),atom(*reinterpret_cast<uint64_t*>(input+0x40)).load(),messages[1],messages[2],messages[3],messages[4],messages[5],messages[6],messages[7],messages[8],messages[9],messages[10],messages[11]);
    }
    // Synthetic diagnostic input uses precisely the v11 route. It is not a
    // focused physical input implementation, and cannot enable native control.
    static std::string last;std::ifstream in(directory+"/live-input.txt");std::string line;std::getline(in,line);
    if(line!=last&&!line.empty()){last=line;std::istringstream args(line);uint32_t type=0,code=0;int a=0,b=0;std::string token;
        if(args>>token>>type>>code>>a>>b&&type>=1&&type<=6&&code<=65535)push({uint16_t(type),uint16_t(code),a,b,0});
    }
}
}
