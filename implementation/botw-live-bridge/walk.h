// Link walked by the game, part 1: the game's left stick follows Steve's movement, because
// enemies find Link by the sounds of his steps, and a Link who is only put in place makes none. The bridge still puts Link's body where Steve stands
// after the game's physics (body.h); the stick only lets the game run its own walk, run and sneak states, which
// make its noise (Link's AI program: PlayerMoveNoise, PlayerLandNoise, PlayerEmitEquipmentNoise by the stepping
// foot) and its step sounds.
//
// The stick is Steve's real movement of the last tick (not the keys): a Steve stopped by a wall pushes nothing,
// so the game is not asked to climb it. Direction: relative to Steve's look (the game's camera is Minecraft's,
// game_camera.h) or to the game's own camera (lab/walk.txt, the second number). Size: Steve's speed over a full
// walk (4.317 blocks a second, Minecraft's), at most 1. Steve's sneak is the game's crouch (the left stick's
// press toggles it; Link's state is actor+0x8DC bit 2, BetterVR's IS_CROUCHING).
// Not while climbing (the bridge's climb), swimming, flying, a screen, or dead.
//
// lab/walk.txt: <mode> <reference>. mode 0 off, 1 the stick and the crouch, 2 (the default) also Steve's sprint
// as the game's dash (B held; it costs Link's stamina). reference 0 Steve's look, 1 the
// game's camera.
namespace walk {
static std::atomic<int> mode{2},reference{0};
// When the dash was last held (GetTickCount64): the game's own stamina use is let through then (physical.h).
static std::atomic<uint64_t> dashAt{0};
static bool dashing(){return GetTickCount64()-dashAt.load()<300;}
constexpr uint32_t kStickPress=0x40000u,kDash=0x4000u,kCrouching=4u;
struct Out{float x=0,y=0;uint32_t bits=0;};
static void poll(){
    static uint64_t checked=0;if(GetTickCount64()-checked<500)return;checked=GetTickCount64();
    int m=2,r=0;std::ifstream file(directory+"/walk.txt");file>>m>>r;mode=m>=0&&m<=2?m:2;reference=r?1:0;
}
static Out step(const botwcraft::proto::McState& s,bool fresh){
    Out out;static uint64_t said=0,toggled=0;
    if(!mode.load()||!fresh)return out;
    namespace p=botwcraft::proto;
    uint32_t linkFlags=0;
    if(uint32_t actor=body::identityActor.load();actor&&body::readable(actor+0x8DC,4))linkFlags=body::u32(actor+0x8DC);
    bool idle=(s.flags&(p::kMcScreenOpen|p::kMcDead|p::kMcSwimming|p::kMcFlying|p::kMcClimbing))||!(s.flags&p::kMcInWorld);
    double dx=s.curX-s.prevX,dz=s.curZ-s.prevZ,seconds=(s.tickMs>1?s.tickMs:50)/1000.0;
    float speed=float(std::sqrt(dx*dx+dz*dz)/seconds);
    float facing=reference.load()?gameYaw.load():s.yaw;
    float moving=float(std::atan2(-dx,dz)*57.2957795);
    float push=0,relative=0;
    if(!idle&&speed>.2f&&speed<30){
        push=std::min(1.f,speed/4.317f);relative=(moving-facing)/57.2957795f;
        out.x=push*std::sin(relative);out.y=push*std::cos(relative);
        if(mode.load()==2&&(s.flags&p::kMcSprinting)){out.bits|=kDash;dashAt=GetTickCount64();}
    }
    // The crouch: one press of the stick when Steve's sneak and Link's crouch differ, at most every 0.4 s (the game
    // takes a moment to change; a second press too soon would undo the first).
    bool sneak=!idle&&(s.flags&p::kMcSneaking),crouched=linkFlags&kCrouching;
    auto now=GetTickCount64();
    if(sneak!=crouched&&now-toggled>=400&&!(linkFlags&2)){out.bits|=kStickPress;toggled=now;}
    if(now-said>=250){said=now;event("{\"event\":\"walk_stick\",\"speed\":%.2f,\"moving\":%.1f,\"facing\":%.1f,\"steve_yaw\":%.1f,\"game_yaw\":%.1f,\"stick\":[%.3f,%.3f],\"bits\":%u,\"link_flags\":%u,\"mc_flags\":%u}",speed,moving,facing,s.yaw,gameYaw.load(),out.x,out.y,out.bits,linkFlags,s.flags);}
    return out;
}
}
