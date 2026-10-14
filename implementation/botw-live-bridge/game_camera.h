// BOTW's own camera for the frame. The render selectors (probe.cpp) hand Minecraft's camera
// to the passes that draw; whatever the game itself places or culls around its camera
// (weather and wind volumes, effects, the listener) reads the camera actor instead, and that
// one still followed the hidden Link from behind. Seen from Minecraft's camera such a volume
// shows its edge (the "sky artifact" of report 29; the play session of 2026-10-10).
//
// BetterVR writes its headset pose into the same object at the same two instructions "for
// gameplay and culling reasons" (patch_CTRL_CameraControls.asm,
// updateCameraPositionAndTarget). This is that hook with Minecraft's camera.
//
// US v208, the end of the camera actor's update (rpx_dis.py 0x02C05480): its final look-at
// at +0x5C0 (position, target, up: three floats each) is copied from one of two sources.
// 0x02C054FC `stfs f0,0x5CC(r31)` and 0x02C05590 `stfs f13,0x5C4(r31)` are the last store of
// each branch; the patch repeats the store and calls here with r3 the actor, r4 the branch.
namespace gameCamera {
// lab/game-camera.txt: 0 observe only, 1 (default) write Minecraft's camera.
static std::atomic<int> mode{1};
static void poll(){
    static uint64_t checked=0;if(GetTickCount64()-checked<500)return;checked=GetTickCount64();
    int value=1;std::ifstream file(directory+"/game-camera.txt");if(!(file>>value))value=1;
    mode=value!=0;
}
// The view the selectors of this frame will be given: viewTarget, then what physicalFrame
// makes of it (physical.h), without that function's publication to Minecraft.
static bool minecraftView(ViewProbe& view){
    if(scenePresent.load()||!viewTarget(view))return false;
    if(!physicalActive.load()||physicalBlocked.load())return true;
    physical::unpack(physicalAngles.load(),view.yaw,view.pitch);
    if(body::command.load()!=5)return true;
    auto applied=appliedTarget.load();
    if(!applied||!liveBodyLease.load()||applied->request.bodyEpoch!=body::lifecycleEpoch.load()||applied->request.collisionEpoch!=liveCollisionEpoch.load())return true;
    for(int i=0;i<3;i++)view.eye[i]=applied->feet[i]+(i==1?applied->eyeHeight:0);
    botwcraft::proto::McState newest=applied->request.source;liveMcNow(newest);
    view.view=int(newest.cameraMode);view.distance=newest.cameraDistance*applied->request.scale;
    return true;
}
static void update(CPU* c){
    c->ip=c->spr.lr;
    static uint64_t calls=0;static int last=-1;
    uint32_t actor=c->gpr[3];uint8_t data[36];
    if(!cameraProbe.load()||!read(actor+0x5C0,data,sizeof(data)))return;
    float native[9];for(int i=0;i<9;i++)native[i]=real(data+4*i);
    {float f[3]{native[3]-native[0],native[4]-native[1],native[5]-native[2]},d=std::sqrt(f[0]*f[0]+f[1]*f[1]+f[2]*f[2]);
    if(std::isfinite(d)&&d>.01f){gameYaw=std::atan2(-f[0],f[2])*57.2957795f;gamePitch=-std::asin(std::clamp(f[1]/d,-1.f,1.f))*57.2957795f;gameViewTick=GetTickCount64();}}
    ViewProbe view;float forward[3]{},right[3]{},up[3]{};
    bool own=minecraftView(view),written=false;
    if(own){
        viewAxes(view,forward,right,up);for(int i=0;i<3;i++)steveEye[i]=view.eye[i];
        // The target keeps the distance the game chose: nothing here depends on it, the game might.
        float reach=0;for(int i=0;i<3;i++)reach+=(native[3+i]-native[i])*(native[3+i]-native[i]);
        reach=std::isfinite(reach)&&reach>1?std::sqrt(reach):1;
        bool finite=true;for(int i=0;i<3;i++)finite=finite&&std::isfinite(view.eye[i])&&std::isfinite(forward[i])&&std::isfinite(up[i]);
        if(finite&&mode.load()){
            for(int i=0;i<3;i++){storeReal(data+4*i,view.eye[i]);storeReal(data+12+4*i,view.eye[i]+forward[i]*reach);storeReal(data+24+4*i,up[i]);}
            memcpy(static_cast<uint8_t*>(getGuest())+actor+0x5C0,data,sizeof(data));written=true;
        }
    }
    // One record in fifteen, and every change of what is done.
    int state=int(own)|int(written)<<1;
    if(calls++%15&&state==last)return;
    last=state;
    float apart=0;for(int i=0;i<3;i++)apart+=(native[i]-view.eye[i])*(native[i]-view.eye[i]);
    event("{\"event\":\"game_camera\",\"actor\":%u,\"branch\":%u,\"own\":%d,\"written\":%d,\"native_eye\":[%.9g,%.9g,%.9g],\"native_at\":[%.9g,%.9g,%.9g],\"native_up\":[%.6g,%.6g,%.6g],\"eye\":[%.9g,%.9g,%.9g],\"forward\":[%.6g,%.6g,%.6g],\"view\":%d,\"apart\":%.6g}",
        actor,c->gpr[4],int(own),int(written),native[0],native[1],native[2],native[3],native[4],native[5],native[6],native[7],native[8],
        view.eye[0],view.eye[1],view.eye[2],forward[0],forward[1],forward[2],view.view,own?std::sqrt(apart):-1.f);
}
}
