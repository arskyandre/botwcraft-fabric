// Task E: accepted D d5d294b6, C a8b3c0e and B c2a1f72; see NOTICE.
#define NOMINMAX
#include <windows.h>
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstdarg>
#include <cmath>
#include <atomic>
#include <fstream>
#include <string>
#include <array>
#include <sstream>
#include <memory>
#include <unordered_set>
#include "vendor/vkroots.h"

struct CPU {
    uint32_t ip,gpr[32]; alignas(8) double fpr[64]; uint32_t fpscr;
    uint8_t cr[32],ca,lsqe,pse; int32_t remaining,skipped;
    struct { uint32_t lr,ctr,xer,upir,gqr[8]; } spr;
};
static_assert(offsetof(CPU,spr)==0x2B8);
using Register=void(*)(const char*,const char*,void(*)(CPU*));
static thread_local uint8_t* guest;
static void*(*getGuest)();
static uint64_t(*getTitle)();
static std::string directory;
static std::atomic<bool> fullEvidence{false};
// observer.txt 2: play without evidence. The per-update and per-harvest trace
// rows and the rolling harvest JSON are not written; lifecycle events remain.
static std::atomic<bool> quietTrace{false};
static std::atomic<int> inputAction{0};
static std::atomic<ULONGLONG> inputUntil{0};

// Bounded MPSC reservations across PPC/Vulkan/input threads; logger owns disk.
static constexpr size_t capacity=8192;
static char events[capacity][2048];
static std::atomic<size_t> head{0},tail{0};
static std::atomic<bool> eventReady[capacity]{};
static std::atomic<uint64_t> drops{0};
static std::atomic<uint64_t> targetEpoch{1};
// Host size of the scene targets last drawn into (the guest's 1280x720 until a
// resolution pack is seen). Minecraft is asked for its hand/HUD at this size.
static std::atomic<uint32_t> hostSceneW{1280},hostSceneH{720};
enum class Reject {draw, noMarker, markerFrame, markerPresent, markerRange,
    missingImage, imageFormat, imageExtent, imageLayout, activePass,
    cameraBusy, cameraFrame, cameraEpoch, projection, gameplay, bodyEpoch,
    fixture, resources, disabled, anchorBusy, renderError, count};
static const char* rejectNames[]={"draw","no_marker","marker_frame","marker_present","marker_range",
    "missing_image","image_format","image_extent","image_layout","active_pass",
    "camera_busy","camera_frame","camera_epoch","projection","gameplay","body_epoch",
    "fixture","resources","disabled","anchor_busy","render_error"};
static std::atomic<uint64_t> rejects[size_t(Reject::count)]{};
static std::atomic<uint64_t> cameraReadRejects{0},cameraPublishBusy{0};
static void event(const char* fmt,...) {
    if(quietTrace.load()&&(strstr(fmt,"\"live_body_write\"")||strstr(fmt,"\"ground_check\"")||strstr(fmt,"\"native_region_request\"")||strstr(fmt,"\"native_shape_probe\"")||
        strstr(fmt,"\"submit_result\"")||strstr(fmt,"\"composition\"")||strstr(fmt,"\"native_render_projection\"")||strstr(fmt,"\"player_material\"")||
        strstr(fmt,"\"scene_targets\"")||strstr(fmt,"\"present\"")||strstr(fmt,"\"ipc_overlay\"")||strstr(fmt,"\"applied_scene\"")))return;
    if(!fullEvidence.load()&&(strstr(fmt,"\"camera\"")||strstr(fmt,"\"color_marker\"")||strstr(fmt,"\"depth_marker\"")||strstr(fmt,"\"attachment_marker\"")||strstr(fmt,"\"draw_camera\"")))return;
    size_t h=head.load(std::memory_order_relaxed);bool owned=false;
    for(int tries=0;tries<8;tries++){
        if(h-tail.load(std::memory_order_acquire)>=capacity){++drops;return;}
        if(head.compare_exchange_weak(h,h+1,std::memory_order_relaxed)){owned=true;break;}
    }
    if(!owned){++drops;return;}
    va_list args;va_start(args,fmt);vsnprintf(events[h%capacity],2048,fmt,args);va_end(args);
    eventReady[h%capacity].store(true,std::memory_order_release);
}
struct CpuCounter {std::atomic<uint64_t> calls{0},ticks{0},maximum{0};};
static CpuCounter selectorCpu,opacityCpu;
static CpuCounter bodyMatchCpu,bodySampleCpu,bodyClaimCpu,bodyPostCpu,inputCpu,cameraCpu,vulkanCpu,presentCpu;
static std::atomic<uint64_t> paceFrames{0},paceLate{0},paceTicks{0},paceMax{0};
// Work time of a frame: from the game's controller read (start of its frame)
// to the present, i.e. without the wait for the vertical blank. 3 ms buckets.
static std::atomic<int64_t> frameStartQpc{0};static std::atomic<uint64_t> workBuckets[14]{},workTicks{0},workFrames{0};
// Where a frame's time goes (frame_timeline, lab/frame-timeline.txt 1): when
// each bridge callback first ran since the previous present, and on which thread.
namespace frameMarks {
enum {kInput,kCamera,kSample,kPost,kHarvest,kHarvestEnd,kDraw,kWait,kStart,kCount};
static std::atomic<bool> enabled{false};
static std::atomic<int64_t> at[kCount]{};static std::atomic<uint32_t> thread[kCount]{};
static void mark(int k){
    if(!enabled.load(std::memory_order_relaxed)||at[k].load(std::memory_order_relaxed))return;
    LARGE_INTEGER q;QueryPerformanceCounter(&q);int64_t expected=0;if(at[k].compare_exchange_strong(expected,q.QuadPart))thread[k]=GetCurrentThreadId();
}
// Every call of a few callbacks by whole milliseconds since the last present
// (frame_phase): where in its frame the game computes, builds its drawing and
// the GPU thread records it.
enum {pMatch,pPost,pInput,pSelector,pOpacity,pPass,pSubmit,pCount};
static const char* const phaseNames[pCount]={"match","post","input","selector","opacity","render_pass","submit"};
static std::atomic<int64_t> presentQpc{0};static std::atomic<uint32_t> phase[pCount][60]{};
static void tally(int k){
    if(!enabled.load(std::memory_order_relaxed))return;
    static const int64_t hz=[]{LARGE_INTEGER f;QueryPerformanceFrequency(&f);return f.QuadPart;}();
    int64_t p=presentQpc.load(std::memory_order_relaxed);if(!p)return;
    LARGE_INTEGER q;QueryPerformanceCounter(&q);int64_t ms=(q.QuadPart-p)*1000/hz;if(ms>=0)++phase[k][std::min<int64_t>(59,ms)];
}
}
// BOTW's frame ends in sead::GameFrameworkCafe::waitForVsync (0x031FACC4):
//   do { GX2WaitForVsync(); GX2GetSwapStatus(&swaps,&flips,..); } while (flips < swaps);
// Before it, presentAndDrawDone makes the GPU wait for the next vertical blank
// (GX2SetGPUFence on a blank counter) and swaps. Cemu shows the swap when that
// wait ends (Cemu source: IT_WAIT_REG_MEM, IT_HLE_TRIGGER_SCANBUFFER_SWAP) and
// for this title advances the flip count every swap interval whether a swap
// is pending or not (LatteTiming_signalVsync, title branch). So `flips < swaps`
// does not hold here, the loop is one more blank of waiting, and the frame has
// the 16.7 ms between that blank and the next to work in: nothing runs in the
// first 16 ms after a present (route-50), and a frame that needs 17 ms is
// shown after 50 instead of 33.
// The pack routes each step of the loop here. Returned in r3: 0 the frame may
// begin, 1 wait for a vertical blank, 2 sleep r4 timer ticks and ask again.
// lab/frame-pace.txt: 0 the native order; N the frame begins N ms before the
// next flip is due (its work budget), still one frame per two blanks.
// Nothing in Cemu keeps two swaps a swap interval apart: the native 30 FPS is
// the two waits, one blank each (routes 52/53: a 30 ms budget ran at 39 FPS,
// frames shown alternately after 17 and 33 ms). So the pack also asks before
// the game reads the blank counter for that GPU wait (Bridge_SwapPace): a
// frame ready before the blank in the middle of its period is held until that
// blank has passed, and its wait then ends at the blank it is due at.
// One frame per flip period, shown at its end.
// kBlanksPerFrame is the game's swap interval; nothing else here assumes 30 FPS.
namespace framePace {
constexpr int kDefaultBudgetMs=25,kBlanksPerFrame=2;
constexpr double kBlankMs=1000/59.94,kFlipPeriodMs=kBlanksPerFrame*kBlankMs,kTimerTicksPerMs=62156.25; // OSTime ticks: bus clock / 4
static std::atomic<int> budgetMs{kDefaultBudgetMs};
static std::atomic<uint64_t> frames{0},early{0},held{0};
// The game's main thread only.
static uint32_t waits=0,flipAtBoundary=0;static bool slept=false;static int64_t boundary=0;
static void pace(CPU* c){
    c->ip=c->spr.lr;uint32_t swaps=c->gpr[3],flips=c->gpr[4],action=0,ticks=0;
    static const int64_t hz=[]{LARGE_INTEGER f;QueryPerformanceFrequency(&f);return f.QuadPart;}();
    LARGE_INTEGER now;QueryPerformanceCounter(&now);
    if(!waits&&!slept)frameMarks::mark(frameMarks::kWait);
    if(int32_t(swaps-flips)>0)action=1;
    else if(!waits&&!slept){
        action=1; // the native loop waits once before it looks
        // `boundary`: the blank at which a frame's flip was first seen done. The next frame may begin
        // early only a full flip period after it (never two frames inside one period, whatever the
        // emulator does with the swap interval).
        bool flipped=flips!=flipAtBoundary,period=(now.QuadPart-boundary)*1000>=25*hz;int budget=budgetMs.load();
        if(flipped&&period){
            boundary=now.QuadPart;flipAtBoundary=flips;
            if(budget>0){
                double sleepMs=kFlipPeriodMs-1.0-budget; // called about 1 ms after the blank
                if(sleepMs>=1){action=2;ticks=uint32_t(sleepMs*kTimerTicksPerMs);slept=true;}else action=0;
                ++early;
            }
        }
    }
    if(action==1)++waits;
    if(!action){waits=0;slept=false;++frames;frameMarks::mark(frameMarks::kStart);}
    c->gpr[3]=action;c->gpr[4]=ticks;
}
// Before the game reads the blank counter for its present. r3: 0 go on, 2 sleep r4 timer ticks and ask again.
static void swap(CPU* c){
    c->ip=c->spr.lr;c->gpr[3]=0;c->gpr[4]=0;
    if(budgetMs.load()<=0||!boundary)return;
    static const int64_t hz=[]{LARGE_INTEGER f;QueryPerformanceFrequency(&f);return f.QuadPart;}();
    LARGE_INTEGER now;QueryPerformanceCounter(&now);double since=double(now.QuadPart-boundary)*1000/hz;
    // The last blank before the one this frame is due at, plus a margin for the boundary's own lateness.
    double until=kFlipPeriodMs-kBlankMs+.5-since;
    if(since<0||since>=kFlipPeriodMs||until<=0)return;
    static int64_t counted=0;if(counted!=boundary){counted=boundary;++held;}
    c->gpr[3]=2;c->gpr[4]=uint32_t(std::max(until,.3)*kTimerTicksPerMs);
}
}
struct CpuScope {
    CpuCounter& counter;LARGE_INTEGER start;
    explicit CpuScope(CpuCounter& c):counter(c){QueryPerformanceCounter(&start);}
    ~CpuScope(){LARGE_INTEGER end;QueryPerformanceCounter(&end);uint64_t n=end.QuadPart-start.QuadPart;++counter.calls;counter.ticks.fetch_add(n);uint64_t m=counter.maximum.load();while(n>m&&!counter.maximum.compare_exchange_weak(m,n)){} }
};
#include "live_target.h"
#include "native_regions.h"
#include "native_mesh.h"
#include "trace.h"
namespace walking {static void poll();static void configure(botwcraft::proto::BotwState&);}
#include "body.h"
#include "calibration.h"
#include "native_movers.h"
#include "native_shapes.h"
struct Camera {
    uint32_t id=0,cam=0,proj=0,buffer=0,sceneW=0,sceneH=0;uint64_t qpc=0,epoch=0,bodySeq=0,bodyEpoch=0;uint32_t actor=0;bool authority=false,gameplay=false;
    float mcOrigin[3]{},mcAnchor[3]{},mcScale=1;bool common=false;
    // Minecraft's view (0 first person, 1 behind, 2 in front) and the native feet its camera follows.
    int mcView=0;float feet[3]{};bool hasFeet=false;
    bool scene=false;   // a scene of the game's: this is its own camera, and the feet are Link's
    float view[12]{},projection[16]{},device[16]{},eye[3]{},target[3]{},nearZ=0,farZ=0,fov=0,aspect=0,zScale=0,zOffset=0;
};
static bool sceneProjection(const Camera& s){
    // Scene identity comes from the PPC post-HDR marker, native projection
    // vtable checked in snapshot(), matching scene attachments and epochs.
    // Native camera collision can legitimately reduce nearZ to 0.1.
    for(float v:s.device)if(!std::isfinite(v))return false;
    if(!(std::isfinite(s.nearZ)&&std::isfinite(s.farZ)&&s.nearZ>0&&s.farZ>s.nearZ&&
        std::isfinite(s.fov)&&s.fov>0&&s.fov<3.14f&&std::isfinite(s.aspect)&&s.aspect>.1f&&s.aspect<10))return false;
    float fy=1/std::tan(s.fov*.5f);
    if(std::abs(s.device[0]-fy/s.aspect)>.0025f*fy||std::abs(s.device[5]-fy)>.0025f*fy)return false;
    if(std::abs(s.device[12])>1e-6f||std::abs(s.device[13])>1e-6f||std::abs(s.device[14]+1)>1e-6f||std::abs(s.device[15])>1e-6f)return false;
    auto depth=[&](float z){return .5f*(s.device[10]*-z+s.device[11])/z+.5f;};
    return std::abs(depth(s.nearZ))<.002f&&std::abs(depth(s.farZ)-1)<.002f;
}
static Camera cameras[256];
static std::atomic_flag cameraBusy=ATOMIC_FLAG_INIT;
static bool read(uint32_t p,void* out,size_t n) {
    auto base=static_cast<uint8_t*>(getGuest());
    if(!base||p<0x10000000||uint64_t(p)+n>0xA0000000||(p&3)||n>0xB8)return false;
    SIZE_T copied=0;bool ok=ReadProcessMemory(GetCurrentProcess(),base+p,out,n,&copied)&&copied==n;
    if(!ok)event("{\"event\":\"read_failed\",\"base\":%llu,\"p\":%u,\"bytes\":%zu,\"error\":%lu}",(uint64_t)base,p,n,GetLastError());
    return ok;
}
static uint32_t word(const uint8_t* p){uint32_t v;memcpy(&v,p,4);return __builtin_bswap32(v);}
static float real(const uint8_t* p){uint32_t v=word(p);float f;memcpy(&f,&v,4);return f;}
static void put32(uint32_t p,uint32_t v){v=__builtin_bswap32(v);memcpy(static_cast<uint8_t*>(getGuest())+p,&v,4);}
static void putf(uint32_t p,float f){uint32_t v;memcpy(&v,&f,4);put32(p,v);}
static std::atomic<bool> cameraProbe{false};
static std::atomic<uint64_t> peerViewAt{0};
static std::atomic<uint64_t> peerDiedAt{0};   // when Minecraft's player last died (live.h: kEvPlayerDied)
static std::atomic<uint32_t> peerDeathCause{0},peerDeathAttacker{0};   // what killed him (botwcraft::proto::DeathCause) and the game's attacker (0: none)
static std::atomic<uint32_t> sceneCameraPointer{0},sceneProjectionPointer{0};
// Bounded camera-only experiment; explicit native coordinates, NOT a live
// calibrated transform. No body control. Lease expires after at most 5s.
struct ViewProbe {float origin[3]{},anchor[3]{},scale=1;float eye[3]{},yaw=0,pitch=0,fov=70,aspect=0;uint64_t id=0,until=0,epoch=0,target=0,peerSession=0;uint32_t actor=0,procId=0,mcSeq=0;bool common=false;
    // Third person (F5): `eye`, `yaw` and `pitch` stay the player's; the camera sits `distance`
    // native units behind the eye (view 1) or in front of it looking back (view 2), as
    // Minecraft's own camera does after its zoom against blocks and native collision.
    int view=0;float distance=0,feet[3]{};bool hasFeet=false;};
static ViewProbe viewProbe;
static std::atomic<bool> physicalActive{false},physicalBlocked{false};
// The game's own buttons held on the keyboard (physical.h keyEvent), as the pad's bits: written into the pad the game
// reads while the keyboard and mouse are Minecraft's (input(), below).
static std::atomic<uint32_t> padHeld{0};
// H held in play (physical.h keyEvent): the whistle, the cross's down.
static std::atomic<bool> whistleHeld{false};
// Lab (looking at what a button of the game's does in play before a key is given to it): lab/pad.txt holds the
// pad's bits in hexadecimal, held for as long as the file holds them; the cross's four go as the cross here.
static std::atomic<uint32_t> labPad{0};
// Lab (the runes, before a design): the game's own camera, which only the right stick turns, made to follow Steve's
// look through that stick. gameCamera::update gives where it looks; input() pushes the stick by what is missing.
// lab/look-stick.txt: 0 off, 1 on; then the sign of the stick up and down (-1: route-226), and the degrees that are a full push.
// Lab: the game's rune manager (BetterVR, cemu_hooks.h: *10463798, its flags at +4C, the chosen rune at +1D4). Read at
// every pad sample and told when it changes; lab/rune-select.txt holds a number and a rune, written once as the rune
// wanted next (+1D0; written as the chosen itself, the manager put the old one back within a tenth of a second: route-232).
static std::atomic<int> runeSelect{-1};
static std::atomic<int> lookStick{0},lookStickUp{-1};static std::atomic<float> lookStickFull{20.f},gameYaw{0},gamePitch{0},steveEye[3];static std::atomic<uint64_t> gameViewTick{0};
// An event of the game's is running (a talk, a scene; gameEvents(), read at every pad sample).
static std::atomic<bool> gameEvent{false};
// The right stick pushed left or right, as the pad's own bits for it (physical.h screenButton: the menu keys).
constexpr uint32_t kRightStickLeft=0x04000000u,kRightStickRight=0x02000000u;
// Steve's hearts and Link's (physical.h), at every pad sample.
namespace nativeDamage{static void hearts();static void climbStamina();}
namespace hudHide{static void apply();}
namespace actors{static bool hitSource(uint32_t& id,bool& thrown,char* name);}
static std::atomic<float> physicalYaw{0},physicalPitch{0};
static std::atomic<uint64_t> physicalAngles{0};
static std::atomic_flag botwWriteBusy=ATOMIC_FLAG_INIT;
static std::atomic<std::shared_ptr<const ViewProbe>> physicalSeed;
static void physicalFrame(uint32_t,ViewProbe&);
static std::atomic_flag viewProbeBusy=ATOMIC_FLAG_INIT;
static bool viewTarget(ViewProbe& result){
    if(!body::allowed()||viewProbeBusy.test_and_set(std::memory_order_acquire))return false;
    result=viewProbe;viewProbeBusy.clear(std::memory_order_release);
    auto now=GetTickCount64();
    return result.id&&now<result.until&&now-peerViewAt.load()<500&&now-body::sampleAt.load()<500&&
        result.epoch==body::lifecycleEpoch.load()&&result.target==targetEpoch.load()&&
        result.actor==body::player.load()&&result.actor==body::identityActor.load()&&
        body::readable(result.actor+0x50,4)&&body::u32(result.actor+0x50)==result.procId;
}
struct AppliedView {uint32_t frame=0,cam=0,proj=0;ViewProbe request;};
static AppliedView appliedView;
static std::atomic_flag appliedBusy=ATOMIC_FLAG_INIT;
static bool selectTarget(uint32_t frame,ViewProbe& result){
    if(scenePresent.load())return false;   // a scene of the game's: its own camera in every pass
    ViewProbe fresh;if(!viewTarget(fresh)||!frame)return false;
    if(appliedBusy.test_and_set(std::memory_order_acquire))return false;
    if(appliedView.frame!=frame){physicalFrame(frame,fresh);appliedView={frame,0,0,fresh};}
    result=appliedView.request;
    bool valid=result.epoch==fresh.epoch&&result.target==fresh.target&&result.actor==fresh.actor&&
        result.procId==fresh.procId;
    // Freeze the view across selectors and the light prepass for this scene.
    // Renewal may extend expiry; it cannot change the view halfway through it.
    appliedBusy.clear(std::memory_order_release);return valid;
}
static void recordApplied(uint32_t frame,uint32_t pointer,bool projection){
    if(appliedBusy.test_and_set(std::memory_order_acquire))return;
    if(appliedView.frame==frame){if(projection)appliedView.proj=pointer;else appliedView.cam=pointer;}
    appliedBusy.clear(std::memory_order_release);
}
#include "player_render.h"
// A scene of the game's (live_target.h): the frame is drawn with the game's own camera. This says where Minecraft's
// world is in it and where Steve's body stands (Link's feet), for the draws inside the frame (integrate.h) and the
// one at the marker.
static bool sceneRequest(ViewProbe& r){
    if(!scenePresent.load())return false;
    r={};r.common=true;r.scale=float(nativeRegions::scale);r.view=1;r.epoch=body::lifecycleEpoch.load();r.target=targetEpoch.load();r.actor=body::player.load();
    for(int i=0;i<3;i++){r.origin[i]=float(nativeRegions::origin[i]);r.anchor[i]=float(nativeRegions::anchor[i]);}
    if(body::readable(r.actor+0x1F8,0x30)){r.hasFeet=true;for(int i=0;i<3;i++){r.feet[i]=body::f32(r.actor+0x1F8+12+i*16);r.hasFeet&=std::isfinite(r.feet[i]);}}
    return true;
}
namespace integrate {static void earlyView(uint32_t,const uint8_t*,const ViewProbe&,bool scene=false);static void earlyProjection(uint32_t,const uint8_t*,const ViewProbe&);static void poll();}
static void storeReal(uint8_t* dst,float f){uint32_t v;memcpy(&v,&f,4);v=__builtin_bswap32(v);memcpy(dst,&v,4);}
// Minecraft's Camera.setup: the front view turns half round and mirrors the pitch, then either
// detached view moves the eye back along its own forward.
static void viewAxes(ViewProbe& target,float forward[3],float right[3],float up[3]){
    float yaw=(target.view==2?target.yaw+180:target.yaw)*.01745329252f,pitch=(target.view==2?-target.pitch:target.pitch)*.01745329252f;
    const float f[3]={-std::sin(yaw)*std::cos(pitch),-std::sin(pitch),std::cos(yaw)*std::cos(pitch)};
    const float r[3]={-std::cos(yaw),0,-std::sin(yaw)},u[3]={-std::sin(yaw)*std::sin(pitch),std::cos(pitch),std::cos(yaw)*std::sin(pitch)};
    for(int i=0;i<3;i++){forward[i]=f[i];right[i]=r[i];up[i]=u[i];if(target.view)target.eye[i]-=f[i]*target.distance;}
}
static void renderCamera(CPU* c){
    CpuScope timing(selectorCpu);frameMarks::tally(frameMarks::pSelector);
    c->ip=c->spr.lr;if(!cameraProbe.load())return;
    // This native object is a derived camera (observed vtable 1028BFC0), not
    // BetterVR's copied base object 1027B294. Observe only the scene pointer
    // independently identified at the previous post-HDR marker; do not write.
    if(c->gpr[3]!=sceneCameraPointer.load())return;
    uint8_t data[0x58];if(!read(c->gpr[3],data,sizeof(data)))return;
    static thread_local uint32_t logged=0;bool logThis=logged!=c->gpr[10];logged=c->gpr[10];
    ViewProbe target;
    if(selectTarget(c->gpr[10],target)&&body::readable(c->gpr[12],sizeof(data),0x10000)){
        // The camera object the game would have drawn with (its own, unless game_camera.h wrote it).
        float nativeEye[3]={real(data+0x34),real(data+0x38),real(data+0x3C)};
        float forward[3],right[3],up[3];viewAxes(target,forward,right,up);
        for(int row=0;row<3;row++){
            auto axis=row==0?right:row==1?up:forward;float dot=0;
            for(int col=0;col<3;col++){float value=row==2?-axis[col]:axis[col];storeReal(data+4*(row*4+col),value);dot+=value*target.eye[col];}
            storeReal(data+4*(row*4+3),-dot);
        }
        auto vtable=__builtin_bswap32(0x1027B294u);memcpy(data+0x30,&vtable,4);
        for(int i=0;i<3;i++){storeReal(data+0x34+i*4,target.eye[i]);storeReal(data+0x40+i*4,target.eye[i]+forward[i]);storeReal(data+0x4C+i*4,up[i]);}
        auto original=c->gpr[3];memcpy(static_cast<uint8_t*>(getGuest())+c->gpr[12],data,sizeof(data));c->gpr[3]=c->gpr[12];
        recordApplied(c->gpr[10],c->gpr[3],false);integrate::earlyView(c->gpr[10],data,target);
        if(logThis)event("{\"event\":\"view_probe_camera\",\"id\":%llu,\"native\":%u,\"copy\":%u,\"lr\":%u,\"epoch\":%llu,\"eye\":[%.9g,%.9g,%.9g],\"yaw\":%.9g,\"pitch\":%.9g,\"native_eye\":[%.9g,%.9g,%.9g]}",target.id,original,c->gpr[3],c->spr.lr,target.epoch,target.eye[0],target.eye[1],target.eye[2],target.yaw,target.pitch,nativeEye[0],nativeEye[1],nativeEye[2]);
    }else{ViewProbe scene;if(sceneRequest(scene))integrate::earlyView(c->gpr[10],data,scene,true);}
    LARGE_INTEGER now;QueryPerformanceCounter(&now);
    uint32_t actor=body::player.load();float box[6]{};
    uint8_t aabb[24];bool hasBox=read(actor+0x34C,aabb,sizeof(aabb));
    if(hasBox)for(int i=0;i<6;i++)box[i]=real(aabb+4*i);
    if(logThis)event("{\"event\":\"native_render_camera\",\"qpc\":%llu,\"camera\":%u,\"lr\":%u,\"body_epoch\":%llu,\"ready\":%d,\"actor\":%u,\"eye\":[%.9g,%.9g,%.9g],\"at\":[%.9g,%.9g,%.9g],\"aabb_valid\":%d,\"aabb\":[%.9g,%.9g,%.9g,%.9g,%.9g,%.9g]}",(uint64_t)now.QuadPart,c->gpr[3],c->spr.lr,body::lifecycleEpoch.load(),int(body::allowed()),actor,real(data+0x34),real(data+0x38),real(data+0x3C),real(data+0x40),real(data+0x44),real(data+0x48),int(hasBox),box[0],box[1],box[2],box[3],box[4],box[5]);
}
static void renderProjection(CPU* c){
    CpuScope timing(selectorCpu);
    static thread_local uint32_t logged=0;bool logThis=logged!=c->gpr[10];logged=c->gpr[10];
    c->ip=c->spr.lr;if(!cameraProbe.load())return;
    if(c->gpr[3]!=sceneProjectionPointer.load())return;
    uint8_t data[0xB8];if(!read(c->gpr[3],data,sizeof(data))||word(data+0x90)!=0x1027B54C)return;
    ViewProbe target;
    if(selectTarget(c->gpr[10],target)&&body::readable(c->gpr[12],sizeof(data),0x10000)){
        float clipNear=.05f,clipFar=real(data+0x98),fov=target.fov*.01745329252f,aspect=target.common?target.aspect:real(data+0xAC);
        if(std::isfinite(clipFar)&&std::isfinite(aspect)&&clipFar>clipNear&&aspect>.1f&&aspect<10&&std::abs(real(data+0x88)-1)<1e-6f&&std::abs(real(data+0x8C))<1e-6f){
            float fy=1/std::tan(fov/2);float matrix[16]{};matrix[0]=fy/aspect;matrix[5]=fy;matrix[10]=-(clipFar+clipNear)/(clipFar-clipNear);matrix[11]=-2*clipFar*clipNear/(clipFar-clipNear);matrix[14]=-1;
            for(int i=0;i<16;i++){storeReal(data+4+4*i,matrix[i]);storeReal(data+0x44+4*i,matrix[i]);}
            data[0]=data[1]=0;storeReal(data+0x94,clipNear);storeReal(data+0x9C,fov);storeReal(data+0xA0,std::sin(fov/2));storeReal(data+0xA4,std::cos(fov/2));storeReal(data+0xA8,std::tan(fov/2));storeReal(data+0xB0,0);storeReal(data+0xB4,0);
            storeReal(data+0xAC,aspect);
            auto original=c->gpr[3];memcpy(static_cast<uint8_t*>(getGuest())+c->gpr[12],data,sizeof(data));c->gpr[3]=c->gpr[12];
            recordApplied(c->gpr[10],c->gpr[3],true);integrate::earlyProjection(c->gpr[10],data,target);
            if(logThis)event("{\"event\":\"view_probe_projection\",\"id\":%llu,\"native\":%u,\"copy\":%u,\"lr\":%u,\"epoch\":%llu,\"near\":%.9g,\"far\":%.9g,\"fov\":%.9g,\"aspect\":%.9g}",target.id,original,c->gpr[3],c->spr.lr,target.epoch,clipNear,clipFar,fov,aspect);
        }
    }else{ViewProbe scene;if(sceneRequest(scene))integrate::earlyProjection(c->gpr[10],data,scene);}
    LARGE_INTEGER now;QueryPerformanceCounter(&now);
    if(logThis)event("{\"event\":\"native_render_projection\",\"qpc\":%llu,\"projection\":%u,\"lr\":%u,\"near\":%.9g,\"far\":%.9g,\"fov\":%.9g,\"aspect\":%.9g}",(uint64_t)now.QuadPart,c->gpr[3],c->spr.lr,real(data+0x94),real(data+0x98),real(data+0x9C),real(data+0xAC));
}
static void lightProjection(CPU* c){
    if(c->gpr[11]!=0x033DBBB8){c->ip=c->spr.lr;return;}
    auto original=c->gpr[3];renderProjection(c);
    event("{\"event\":\"light_projection\",\"frame\":%u,\"original\":%u,\"applied\":%u,\"caller\":%u,\"body_epoch\":%llu,\"target_epoch\":%llu}",c->gpr[10],original,c->gpr[3],c->gpr[11],body::lifecycleEpoch.load(),targetEpoch.load());
}
// lab/frame-start.txt. The pack's count is the word 24 bytes before PostHDR (`sequence`, then five floats), and
// PostHDR is where the branch the pack put at 039B3044 goes. Written only when that word is the number the pack
// has just given, on the game's thread, between two frames' uses of it.
static std::atomic<uint32_t> markerSequence{0},frameStart{0};static std::atomic<bool> markerWhole{false};
static void frameStartNow(uint32_t sequence){
    uint32_t start=frameStart.load();if(!start||sequence>=start)return;
    if(!body::readable(0x039B3044,4,0x01800000))return;
    uint32_t branch=body::u32(0x039B3044);if((branch&0xFE000003)!=0x48000003)return;
    uint32_t at=(branch&0x01FFFFFC)-24;if(!body::readable(at,4,0x01800000)||body::u32(at)!=sequence)return;
    body::put32(at,start);event("{\"event\":\"frame_start\",\"address\":%u,\"from\":%u,\"to\":%u}",at,sequence,start);
}
static void snapshot(CPU* c) {
    c->ip=c->spr.lr;CpuScope timing(cameraCpu);frameMarks::mark(frameMarks::kCamera);frameStartNow(c->gpr[5]);
    uint8_t cam[0x58],proj[0xB8];
    if(!getTitle||getTitle()!=0x00050000101C9400ULL||!read(c->gpr[3],cam,sizeof(cam))||!read(c->gpr[4],proj,sizeof(proj))||word(proj+0x90)!=0x1027B54C){++cameraReadRejects;return;}
    sceneCameraPointer.store(c->gpr[3]);sceneProjectionPointer.store(c->gpr[4]);
    uint32_t actualCam=c->gpr[3],actualProj=c->gpr[4];uint64_t lease=0;bool viewValid=true;
    ViewProbe current;AppliedView applied;
    bool haveApplied=!appliedBusy.test_and_set(std::memory_order_acquire);
    if(haveApplied){applied=appliedView;appliedBusy.clear(std::memory_order_release);}
    bool scene=cameraProbe.load()&&scenePresent.load();
    if(scene);
    else if(cameraProbe.load()&&viewTarget(current)){
        viewValid=haveApplied&&applied.frame==c->gpr[5]&&applied.cam&&applied.proj&&
            applied.request.epoch==body::lifecycleEpoch.load()&&applied.request.target==targetEpoch.load()&&
            applied.request.actor==body::player.load()&&
            body::readable(applied.cam,sizeof(cam),0x10000)&&body::readable(applied.proj,sizeof(proj),0x10000);
        if(viewValid){
            actualCam=applied.cam;actualProj=applied.proj;lease=applied.request.id;
            memcpy(cam,static_cast<uint8_t*>(getGuest())+actualCam,sizeof(cam));
            memcpy(proj,static_cast<uint8_t*>(getGuest())+actualProj,sizeof(proj));
        }
    }else if(haveApplied&&applied.frame==c->gpr[5]&&(applied.cam||applied.proj))viewValid=false;

    // Bounded target observation before the original guest marker call. A
    // missing marker must not silently become a stale-camera draw.
    uint32_t sceneW=0,sceneH=0;
    {
        uint8_t buffer[0x40]{},color[0x24]{},depth[0x24]{};
        bool valid=read(c->gpr[6],buffer,sizeof(buffer));
        uint32_t cp=valid?word(buffer+0x1C):0,dp=valid?word(buffer+0x3C):0;
        bool cv=cp&&read(cp+0xBC,color,sizeof(color)),dv=dp&&read(dp+0xBC,depth,sizeof(depth));
        if(cv&&dv&&word(color+4)==word(depth+4)&&word(color+8)==word(depth+8)){sceneW=word(color+4);sceneH=word(color+8);}
        if(cameraProbe.load())event("{\"event\":\"scene_targets\",\"frame\":%u,\"buffer\":%u,\"color\":%u,\"depth\":%u,\"color_valid\":%d,\"depth_valid\":%d,\"color_extent\":[%u,%u],\"depth_extent\":[%u,%u],\"color_image_bytes\":%u,\"depth_image_bytes\":%u}",c->gpr[5],c->gpr[6],cp,dp,int(cv),int(dv),word(color+4),word(color+8),word(depth+4),word(depth+8),word(color+0x20),word(depth+0x20));
    }
    Camera s;s.id=c->gpr[5];s.cam=c->gpr[3];s.proj=c->gpr[4];s.buffer=c->gpr[6];s.epoch=targetEpoch.load();s.bodySeq=body::controlEpoch.load();s.bodyEpoch=body::lifecycleEpoch.load();s.actor=body::player.load();s.authority=body::active.load();s.gameplay=body::allowed();
    s.cam=actualCam;s.proj=actualProj;s.gameplay&=viewValid;s.sceneW=sceneW;s.sceneH=sceneH;
    if(ViewProbe r;scene&&sceneRequest(r)){
        s.scene=s.common=s.gameplay=true;s.mcScale=r.scale;s.mcView=r.view;s.hasFeet=r.hasFeet;
        std::copy_n(r.origin,3,s.mcOrigin);std::copy_n(r.anchor,3,s.mcAnchor);std::copy_n(r.feet,3,s.feet);
    }
    if(lease){s.common=applied.request.common;s.mcScale=applied.request.scale;std::copy_n(applied.request.origin,3,s.mcOrigin);std::copy_n(applied.request.anchor,3,s.mcAnchor);
        s.mcView=applied.request.view;s.hasFeet=applied.request.hasFeet;std::copy_n(applied.request.feet,3,s.feet);}
    LARGE_INTEGER now;QueryPerformanceCounter(&now);s.qpc=now.QuadPart;
    for(int i=0;i<12;i++)s.view[i]=real(cam+i*4);
    for(int i=0;i<16;i++){s.projection[i]=real(proj+4+i*4);s.device[i]=real(proj+0x44+i*4);}
    for(int i=0;i<3;i++){s.eye[i]=real(cam+0x34+i*4);s.target[i]=real(cam+0x40+i*4);}
    s.nearZ=real(proj+0x94);s.farZ=real(proj+0x98);s.fov=real(proj+0x9C);s.aspect=real(proj+0xAC);s.zScale=real(proj+0x88);s.zOffset=real(proj+0x8C);
    for(float v:s.view)if(!std::isfinite(v))return;
    if(!(s.nearZ>0&&s.farZ>s.nearZ&&s.fov>0&&s.fov<3.14f))return;
    if(cameraBusy.test_and_set(std::memory_order_acquire)){++cameraPublishBusy;return;}
    cameras[s.id%256]=s;cameraBusy.clear(std::memory_order_release);
    if(cameraProbe.load())event("{\"event\":\"applied_scene\",\"frame\":%u,\"lease\":%llu,\"valid\":%d,\"cam\":%u,\"proj\":%u,\"body_epoch\":%llu,\"target_epoch\":%llu,\"eye\":[%.9g,%.9g,%.9g],\"fov\":%.9g,\"aspect\":%.9g}",s.id,lease,int(viewValid),s.cam,s.proj,s.bodyEpoch,s.epoch,s.eye[0],s.eye[1],s.eye[2],s.fov,s.aspect);
    event("{\"event\":\"camera\",\"id\":%u,\"qpc\":%llu,\"cam\":%u,\"proj\":%u,\"buffer\":%u,\"eye\":[%.9g,%.9g,%.9g],\"near\":%.9g,\"far\":%.9g,\"fov\":%.9g,\"aspect\":%.9g,\"zScale\":%.9g,\"zOffset\":%.9g,\"view\":[%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g],\"device\":[%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g]}",s.id,s.qpc,s.cam,s.proj,s.buffer,s.eye[0],s.eye[1],s.eye[2],s.nearZ,s.farZ,s.fov,s.aspect,s.zScale,s.zOffset,s.view[0],s.view[1],s.view[2],s.view[3],s.view[4],s.view[5],s.view[6],s.view[7],s.view[8],s.view[9],s.view[10],s.view[11],s.device[0],s.device[1],s.device[2],s.device[3],s.device[4],s.device[5],s.device[6],s.device[7],s.device[8],s.device[9],s.device[10],s.device[11],s.device[12],s.device[13],s.device[14],s.device[15]);
}
// The frame's number in the marker's alpha. Cemu's clear carries a colour as bytes: (uint32)(a*255.0f) from the
// game's float, then /255.0f for the host, both in single precision. The whole number does not survive that:
// route-193 (a session of 5 h 50 min) read one frame in 32 one too low, n%32==24, and the same arithmetic gives
// one in 4 from frame 65,795, one in 8 from 131,590, one in 16 from 263,180, one in 32 from 526,360. A number
// read one too low is refused at the marker (its stencil differs), and the frame after it was drawn twice, once
// lit and once not (the flash seen in play). So only the low 15 bits go through, exact there (32767*255 < 2^24), and
// the rest is the last number given: the renderer is at most a few frames behind the game.
// For a run: lab/marker-whole.txt 1 is the old way (the whole number as a float, truncated), to show the fault;
// lab/frame-start.txt N puts the pack's count of frames forward to N once, so that a long session's numbers come
// in a minute (frameStartNow, in the camera's call).
constexpr uint32_t kMarkerLow=0x7FFF;
static void markerFloat(CPU* c){c->ip=c->spr.lr;markerSequence.store(c->gpr[5],std::memory_order_release);c->fpr[8]=float(markerWhole.load()?c->gpr[5]:c->gpr[5]&kMarkerLow);}
static uint32_t markerFrame(float alpha){
    if(markerWhole.load())return uint32_t(alpha);
    if(!(alpha>=0&&alpha<float(kMarkerLow)+.5f))return 0;
    uint32_t last=markerSequence.load(std::memory_order_acquire),frame=(last&~kMarkerLow)|uint32_t(alpha+.5f);
    if(frame>last)frame=frame>kMarkerLow?frame-(kMarkerLow+1):0;
    return frame;
}
// The game's event manager (BetterVR 2e06b31 patch_FirstPersonMode_Events.asm: EventMgr::sInstance at 1046D3AC,
// getActiveEventName at 031CA1C0). Read as that function reads (v208, disassembled): the running event is the word
// at manager+175B0, null when none; its name is the string at +8 of the entry its index byte chooses (031D8C64).
static bool liveMcNow(botwcraft::proto::McState& out);
#include "runes.h"
#include "walk.h"
static void gameEvents(){
    bool on=false;uint32_t ev=0;char name[40]="";
    if(body::readable(0x1046D3AC,4)){uint32_t m=body::u32(0x1046D3AC);if(m&&body::readable(m+0x175B0,4))ev=body::u32(m+0x175B0);}
    if(ev){
        on=true;
        if(body::readable(ev,0x190)){
            uint32_t n=body::u32(ev+0x18C),at=ev+0x184+(n<8?n:0),count=body::u32(ev+4),list=body::u32(ev+0xC);
            if(body::readable(at,1)){
                int index=int8_t(guest[at]);
                if(index>=0&&uint32_t(index)<count&&body::readable(list+index*4,4)){
                    uint32_t entry=body::u32(list+index*4);
                    if(body::readable(entry+8,4)){uint32_t text=body::u32(entry+8);
                        // readable() asks for an address of whole words: one check for the whole name.
                        if(body::readable(text&~3u,44))for(int i=0;i<39;i++){char ch=char(guest[text+i]);if(!ch)break;name[i]=isalnum(uint8_t(ch))||ch=='_'||ch=='-'?ch:'?';name[i+1]=0;}}
                }
            }
        }
    }
    static bool was=false;static uint32_t wasEvent=0;
    if(on!=was||(on&&ev!=wasEvent))event("{\"event\":\"game_event\",\"on\":%d,\"object\":%u,\"name\":\"%s\"}",int(on),ev,name);
    was=on;wasEvent=ev;gameEvent=on;sceneOn=on&&sceneWanted.load();
}
static void input(CPU* c) {
    c->ip=c->spr.lr;CpuScope timing(inputCpu);
    {LARGE_INTEGER q;QueryPerformanceCounter(&q);frameStartQpc.store(q.QuadPart,std::memory_order_relaxed);}frameMarks::mark(frameMarks::kInput);frameMarks::tally(frameMarks::pInput);
    guest=static_cast<uint8_t*>(getGuest());
    body::lifecycle();gameEvents();nativeDamage::hearts();nativeDamage::climbStamina();hudHide::apply();
    uint32_t p=c->gpr[3],count=c->gpr[4],err=c->gpr[5];c->gpr[3]=0;
    int a=GetTickCount64()<=inputUntil.load()?inputAction.load():0;
    bool physical=physicalActive.load()&&body::allowed()&&!physicalBlocked.load();
    bool gate=body::active.load()&&body::allowed();
    bool challenge=body::challenge.load()&&gate;
    uint8_t buf[0xAC],e[4];
    if(!read(p,buf,sizeof(buf))||!read(err,e,4)||count<1||count>16)return;
    ++body::inputCalls;
    // Physical + / - and synthetic menu entry disarm before native menu handling.
    if((word(buf)&0xC)||a==5){if(body::command.load()||gate)body::invalidate("menu-input");gate=false;challenge=false;physicalBlocked=true;physical=false;}
    // The keyboard and mouse are Minecraft's: the pad is empty for the game but for the keys that are its own buttons
    // (held, and pressed or let go since the last read).
    static uint32_t padBefore=0;
    if(!physical)runes::rest();
    if(physical){
        // The arrow keys, which are the game's only in an event of its own (physical.h keyEvent), are the left
        // stick here and not the cross: a talk's choices are chosen with the stick.
        // The rune item in Minecraft's hand (runes.h): its presses, the wheel as the cross, the stick for what Magnesis holds.
        botwcraft::proto::McState mcNow{};bool mcFresh=liveMcNow(mcNow);runes::Pad rune=runes::step(mcFresh?mcNow.heldRune:0);
        walk::Out stride=walk::step(mcNow,mcFresh&&!runes::holding.load());   // the game's left stick follows Steve (walk.h)
        uint32_t lab=labPad.load(),held=padHeld.load(),cross=held&0xF00u,keep=~0xF00u;held=(held&keep)|lab|rune.bits|stride.bits|(whistleHeld.load()&&!runes::holding.load()?0x100u:0);keep=~0u;
        uint32_t pressed=held&~padBefore,let=padBefore&~held;
        uint32_t x=cross&0x400?0x3F800000u:cross&0x800?0xBF800000u:0,y=cross&0x200?0x3F800000u:cross&0x100?0xBF800000u:0;
        uint32_t rx=held&kRightStickRight?0x3F800000u:held&kRightStickLeft?0xBF800000u:0;
        uint32_t ry=0;
        if(rune.stick&&!rx){memcpy(&rx,&rune.rx,4);memcpy(&ry,&rune.ry,4);}
        if(!x&&!y&&(stride.x||stride.y)){memcpy(&x,&stride.x,4);memcpy(&y,&stride.y,4);}
        for(uint32_t i=0;i<count;i++){auto q=p+i*0xAC;if(!body::readable(q,28))return;for(int k=0;k<7;k++)put32(q+4*k,0);put32(q,held&keep);put32(q+4,pressed&keep);put32(q+8,let&keep);put32(q+0xC,x);put32(q+0x10,y);put32(q+0x14,rx);put32(q+0x18,ry);}
        if(held!=padBefore)event("{\"event\":\"pad\",\"held\":%u,\"pressed\":%u,\"let_go\":%u,\"stick\":[%d,%d]}",held&keep,pressed&keep,let&keep,x?(x>>31?-1:1):0,y?(y>>31?-1:1):0);
        padBefore=held;put32(err,0);c->gpr[3]=1;++body::gatedCalls;return;
    }
    if(!a&&!gate&&!challenge){
        // Control is not Minecraft's (a screen of the game's own is up): the keyboard's buttons for the game go into
        // what the pad itself gave, or into an empty pad when it gave nothing.
        uint32_t held=padHeld.load()|labPad.load(),pressed=held&~padBefore,let=padBefore&~held;
        if(held||padBefore){
            if(!body::readable(p,28))return;
            if(word(e)){for(int k=0;k<7;k++)put32(p+4*k,0);}
            put32(p,body::u32(p)|held);put32(p+4,body::u32(p+4)|pressed);put32(p+8,body::u32(p+8)|let);
            if(held&(kRightStickLeft|kRightStickRight))put32(p+0x14,held&kRightStickRight?0x3F800000u:0xBF800000u);
            if(held!=padBefore)event("{\"event\":\"pad\",\"held\":%u,\"pressed\":%u,\"let_go\":%u,\"screen\":1}",held,pressed,let);
            padBefore=held;put32(err,0);c->gpr[3]=1;
        }
        return;
    }
    padBefore=0;
    if(a||challenge) {
        uint32_t buttons=a==1?0x8000:a==2?0x0100:a==3?0x0200:a==4?0x4000:a==5?8:a==10?0x10:a==11?0x20:a==12?0x1000:0;
        put32(p,buttons);put32(p+4,buttons);put32(p+8,0);
        for(int i=0;i<4;i++)putf(p+12+i*4,0);
        if(a==6)putf(p+20,.6f);
        if(a==7)putf(p+20,-.6f);
        if(a==8)putf(p+24,.4f);
        if(a==9||challenge)putf(p+16,1);
    }
    if(gate) {
        if(a==9||challenge)++body::challenged;
        putf(p+12,0);putf(p+16,0); // left locomotion only; right camera stays native
        for(int i=0;i<3;i++)put32(p+i*4,body::u32(p+i*4)&~0x6000u); // jump/sprint
        ++body::gatedCalls;
    }
    put32(err,0);c->gpr[3]=1;
}
#include "link_bones.h"
#include "live_scene.h"
#include "render.h"
#include "live.h"
static bool liveMcNow(botwcraft::proto::McState& out){
    // The mapping's lifetime is guarded by the same non-blocking claim the frame publication uses.
    if(botwWriteBusy.test_and_set(std::memory_order_acquire))return false;
    bool ok=false;botwcraft::proto::McState s;
    if(live::base&&live::connected&&!live::failed&&live::seqlock(live::at<live::p::McState>(live::p::kOffMcState),s)&&live::finiteState(s)&&s.seq&&!(s.seq&1)){out=s;ok=true;}
    botwWriteBusy.clear(std::memory_order_release);return ok;
}
#include "finite_patch.h"
#include "native_stream.h"
#include "physical.h"
#include "rune_items.h"
#include "memfind.h"
#include "pouch.h"
#include "save_link.h"
#include "lab_vector.h"
#include "hud_hide.h"
#include "actors.h"
#include "spawn.h"
#include "swing.h"
#include "walls.h"
#include "water.h"
#include "game_camera.h"
template<typename T> static std::vector<T> loadBytes(const std::string& name){
    std::ifstream file(directory+"/fixture/"+name,std::ios::binary|std::ios::ate);if(!file)throw std::runtime_error("missing fixture "+name);auto size=file.tellg();if(size<=0||size%sizeof(T))throw std::runtime_error("invalid fixture "+name);std::vector<T> out(size/sizeof(T));file.seekg(0);file.read(reinterpret_cast<char*>(out.data()),size);if(!file)throw std::runtime_error("short fixture "+name);return out;
}
namespace survey {static void poll();}
static DWORD WINAPI worker(void*) {
    FILE* info=fopen((directory+"/native.txt").c_str(),"w");if(!info)return 1;
    auto exe=GetModuleHandleW(nullptr);
    auto reg=reinterpret_cast<Register>(GetProcAddress(exe,"osLib_registerHLEFunction"));
    getGuest=reinterpret_cast<void*(*)()>(GetProcAddress(exe,"memory_getBase"));
    getTitle=reinterpret_cast<uint64_t(*)()>(GetProcAddress(exe,"gameMeta_getTitleId"));
    if(!reg||!getGuest||!getTitle){fprintf(info,"missing Cemu exports\n");fclose(info);return 2;}
    guest=static_cast<uint8_t*>(getGuest());
    fprintf(info,"initial guest=%p\n",guest);
    reg("coreinit","Bridge_Camera",snapshot);reg("coreinit","Bridge_MarkerFloat",markerFloat);reg("coreinit","Bridge_Input",input);body::initialize(reg);
    reg("coreinit","Bridge_RayPrepare",calibration::prepare);reg("coreinit","Bridge_RayConsume",calibration::consume);
    reg("coreinit","Bridge_ShapeLocked",nativeShapes::locked);
    reg("coreinit","Bridge_ShapeEnter",nativeShapes::enter);
    reg("coreinit","Bridge_ShapeNextQuery",nativeShapes::nextQuery);
    reg("coreinit","Bridge_ShapeBoundsNext",nativeShapes::boundsNext);reg("coreinit","Bridge_ShapeBoundsResult",nativeShapes::boundsResult);
    reg("coreinit","Bridge_WorldLockEntered",nativeShapes::lockEntered);reg("coreinit","Bridge_WorldLockLeft",nativeShapes::lockLeft);
    reg("coreinit","Bridge_ShapeKeys",nativeShapes::keys);reg("coreinit","Bridge_ShapeLeaf",nativeShapes::leaf);reg("coreinit","Bridge_ShapeLeafBounds",nativeShapes::leafBounds);
    reg("coreinit","Bridge_ShapeFinish",nativeShapes::finish);
    reg("coreinit","Bridge_HeightPrepare",nativeShapes::heightPrepare);reg("coreinit","Bridge_HeightResult",nativeShapes::heightResult);
    reg("coreinit","Bridge_TerrainNext",nativeShapes::terrainNext);reg("coreinit","Bridge_TerrainResult",nativeShapes::terrainResult);
    reg("coreinit","Bridge_RenderCamera",renderCamera);reg("coreinit","Bridge_RenderProjection",renderProjection);
    reg("coreinit","Bridge_GameCamera",gameCamera::update);
    reg("coreinit","Bridge_ActorOpacity",playerRender::actorOpacity);
    reg("coreinit","Bridge_WeaponOpacity",playerRender::weaponOpacity);
    reg("coreinit","Bridge_OptionalOpacity",playerRender::weaponOpacity);
    reg("coreinit","Bridge_LightProjection",lightProjection);
    reg("coreinit","Bridge_Health",nativeDamage::health);reg("coreinit","Bridge_Life",nativeDamage::life);reg("coreinit","Bridge_Recover",nativeDamage::recover);reg("coreinit","Bridge_Eaten",nativeDamage::eaten);reg("coreinit","Bridge_Effect",nativeDamage::effect);reg("coreinit","Bridge_Stamina",nativeDamage::stamina);reg("coreinit","Bridge_AddDamage",nativeDamage::addDamage);reg("coreinit","Bridge_DamageReceive",nativeDamage::damageReceive);reg("coreinit","Bridge_Defence",nativeDamage::defence);reg("coreinit","Bridge_ActorHit",water::managerUpdate);reg("coreinit","Bridge_ActorCreate",actors::created);reg("coreinit","Bridge_WaterNext",water::next);reg("coreinit","Bridge_SpawnNext",spawn::next);reg("coreinit","Bridge_SpawnResult",spawn::result);reg("coreinit","Bridge_WeaponSensor",swing::sensor);reg("coreinit","Bridge_WeaponMatrix",swing::matrix);reg("coreinit","Bridge_WeaponTaken",swing::equipSeen);reg("coreinit","Bridge_WallNotice",walls::notice);reg("coreinit","Bridge_WaterResult",water::result);
    reg("coreinit","Bridge_FramePace",framePace::pace);reg("coreinit","Bridge_SwapPace",framePace::swap);
    fprintf(info,"registered PID=%lu CPU_LR_offset=%zu\n",GetCurrentProcessId(),offsetof(CPU,spr));fflush(info);
    FILE* f=fopen((directory+"/frames.jsonl").c_str(),"w");if(!f)return 3;
    try{vertexBytes=loadBytes<uint8_t>("vertices.bin");atlasBytes=loadBytes<uint8_t>("atlas.rgba");overlayBytes=loadBytes<uint8_t>("build.rgba");meshVS=loadBytes<uint32_t>("mesh.vert.spv");meshFS=loadBytes<uint32_t>("mesh.frag.spv");overlayVS=loadBytes<uint32_t>("overlay.vert.spv");overlayFS=loadBytes<uint32_t>("overlay.frag.spv");gbufferFS=loadBytes<uint32_t>("gbuffer.frag.spv");hdrFS=loadBytes<uint32_t>("hdr.frag.spv");shadowFS=loadBytes<uint32_t>("shadow.frag.spv");fixtureReady.store(true,std::memory_order_release);}catch(const std::exception& e){fprintf(info,"fixture unavailable: %s\n",e.what());fflush(info);}
    physical::start();
    std::string last;
    for(;;){
        // body::base is each thread's own: without this, what this loop reads through it (the scene's body, report 79)
        // had the pointer taken when the bridge was loaded, which in some sessions (route-314, route-316) was not usable.
        guest=static_cast<uint8_t*>(getGuest());
        std::ifstream observer(directory+"/observer.txt");int full=0;if(observer>>full){fullEvidence.store(full==1);quietTrace.store(full==2);}observer.close();
        {int timeline=0;std::ifstream(directory+"/frame-timeline.txt")>>timeline;frameMarks::enabled.store(timeline==1);}
        {int budget=framePace::kDefaultBudgetMs;std::ifstream(directory+"/frame-pace.txt")>>budget;framePace::budgetMs.store(std::clamp(budget,0,32));}
        {uint32_t start=0;int whole=0;std::ifstream(directory+"/frame-start.txt")>>start;std::ifstream(directory+"/marker-whole.txt")>>whole;frameStart.store(start);markerWhole.store(whole==1);}
        {int pieces=1;std::ifstream(directory+"/convex-pieces.txt")>>pieces;nativeShapes::convexPieces.store(pieces!=0);}
        {int filters=1;std::ifstream(directory+"/child-filters.txt")>>filters;nativeShapes::childFilters.store(filters!=0);}
        {uint32_t pad=0;std::ifstream(directory+"/pad.txt")>>std::hex>>pad;labPad=pad;}
        // Lab: lab/scan.txt holds a number and a word in hexadecimal; every place in the game's memory (10000000 to
        // 50000000, on four bytes) that holds the word goes to lab/scan-NUMBER.json with the six words around it.
        {static uint32_t scanned=0;uint32_t token=0,want=0,high=0;std::ifstream scan(directory+"/scan.txt");
            if(scan>>token>>std::hex>>want&&token!=scanned&&want){
                scan>>high; // a second word: every word from the first to it, in the program's own data only
                scanned=token;uint32_t big=__builtin_bswap32(want);auto* g=static_cast<uint8_t*>(getGuest());std::vector<uint8_t> chunk(0x10000);std::string out="{\"word\":"+std::to_string(want)+",\"found\":[";int count=0;
                for(uint64_t at=0x10000000;g&&at<(high?0x11000000u:0x50000000u);at+=chunk.size()){
                    SIZE_T copied=0;if(!ReadProcessMemory(GetCurrentProcess(),g+at,chunk.data(),chunk.size(),&copied)||copied!=chunk.size())continue;
                    for(size_t i=0;i<chunk.size();i+=4){uint32_t v;memcpy(&v,chunk.data()+i,4);if((high?__builtin_bswap32(v)<want||__builtin_bswap32(v)>high:v!=big)||count>=2000)continue;
                        char line[160];auto w=[&](int k){uint32_t x=0;if(int64_t(i)+k*4>=0&&i+k*4+4<=chunk.size())memcpy(&x,chunk.data()+i+k*4,4);return __builtin_bswap32(x);};
                        snprintf(line,sizeof(line),"%s[%u,%u,%u,%u,%u,%u,%u,%u]",count++?",":"",uint32_t(at+i),w(-2),w(-1),w(1),w(2),w(3),w(4),w(0));out+=line;}
                }
                out+="]}";std::ofstream(directory+"/scan-"+std::to_string(token)+".json")<<out;
            }}
        {int on=1;std::ifstream(directory+"/runes.txt")>>on;runes::enabled=on!=0;}
        {int on=1;std::ifstream(directory+"/look-throw.txt")>>on;lookThrow::on=on!=0;}
        {int on=1;std::ifstream(directory+"/scene.txt")>>on;sceneWanted=on!=0;}
        {int slate=1;std::ifstream(directory+"/scene-slate.txt")>>slate;sceneSlate=slate;}
        {int on=0;std::ifstream(directory+"/bones.txt")>>on;linkBones::probeOn=on!=0;}
        {int ms=2000;std::ifstream(directory+"/stale-hold.txt")>>ms;staleHoldMs=uint32_t(std::clamp(ms,0,2500));}   // under the 3 s after which a silent peer is a fault
        {std::string kind;std::ifstream(directory+"/wall-borrow.txt")>>kind;if(kind.empty())kind=actors::kBorrowKind;else if(kind=="0")kind.clear();std::lock_guard lock(actors::borrowLock);   // see actors.h
            if(kind.size()<sizeof(actors::borrowName)&&strcmp(actors::borrowName,kind.c_str())){strcpy(actors::borrowName,kind.c_str());actors::borrowSeen=false;}}
        {int mode=1;std::ifstream(directory+"/movers.txt")>>mode;mode=mode<0||mode>2?1:mode;
            // Switched off while running: what Minecraft holds of them is taken back; their regions give them again as they are read.
            if(nativeMovers::mode.exchange(mode)&&!mode){for(const auto& [body,m]:nativeMovers::movers)if(m.told)nativeMovers::gone.push_back(body);nativeMovers::movers.clear();++nativeMovers::revision;}}
        {int hand=0;std::ifstream(directory+"/rune-hand.txt")>>hand;runes::labHand=hand>=1&&hand<=6?uint32_t(hand):0;}
        {int log=0;float turn=0,rise=0,rest=0,wake=0,spread=0,swing=0;std::ifstream file(directory+"/rune-follow.txt");file>>log>>turn>>rise>>rest>>wake>>spread>>swing;
            runes::followLog=log;runes::followTurn=turn>1?turn:70.f;runes::followRise=rise>.1f?rise:15.9f;runes::followRest=rest>0?rest:.3f;runes::followWake=wake>0?std::max(wake,rest):.6f;runes::followSpread=spread>0?spread:150.f;runes::followSwing=swing>0&&swing<1?swing:.15f;}
        {static int taken=0;int token=0,ms=0,quiet=0;float x=0,y=0;std::ifstream file(directory+"/rune-stick.txt");
            if(file>>token>>x>>y>>ms&&token!=taken&&ms>0&&ms<=10000){taken=token;file>>quiet;auto now=GetTickCount64();runes::labStick[0]=std::clamp(x,-1.f,1.f);runes::labStick[1]=std::clamp(y,-1.f,1.f);runes::labStickUntil=now+ms;runes::labStickQuiet=now+ms+std::clamp(quiet,0,10000);}}
        {static int taken=0;int token=0,rune=-1;std::ifstream select(directory+"/rune-select.txt");if(select>>token>>rune&&token!=taken&&rune>=0){taken=token;runeSelect=rune;}}
        {int on=0,up=-1;float full=20;std::ifstream look(directory+"/look-stick.txt");look>>on>>up>>full;lookStick=on;lookStickUp=up>0?1:-1;lookStickFull=full>0?full:20.f;}
        {static uint64_t checked=0;if(GetTickCount64()-checked>=1000){checked=GetTickCount64();double degrees=60;std::ifstream(directory+"/walkable-degrees.txt")>>degrees;support::walkableNy=support::walkableFromDegrees(degrees);}}
        std::ifstream probeConfig(directory+"/camera-probe.txt");int probeEnable=0;probeConfig>>probeEnable;cameraProbe.store(probeEnable==1);
        static uint64_t lastView=0;std::ifstream viewConfig(directory+"/view-probe.txt");ViewProbe candidate;unsigned duration=0;
        if(viewConfig>>candidate.id>>candidate.eye[0]>>candidate.eye[1]>>candidate.eye[2]>>candidate.yaw>>candidate.pitch>>candidate.fov>>duration&&candidate.id!=lastView){
            lastView=candidate.id;bool valid=duration<=5000&&candidate.pitch>=-89&&candidate.pitch<=89&&candidate.fov>=20&&candidate.fov<=120;
            for(float f:{candidate.eye[0],candidate.eye[1],candidate.eye[2],candidate.yaw,candidate.pitch,candidate.fov})valid&=std::isfinite(f)&&std::abs(f)<100000;
            candidate.until=GetTickCount64()+(valid?duration:0);candidate.epoch=body::lifecycleEpoch.load();candidate.target=targetEpoch.load();
            candidate.actor=body::player.load();guest=static_cast<uint8_t*>(getGuest());
            candidate.procId=body::readable(candidate.actor+0x50,4)?body::u32(candidate.actor+0x50):0;
            if(!viewProbeBusy.test_and_set(std::memory_order_acquire)){viewProbe=candidate;viewProbeBusy.clear(std::memory_order_release);}
        }
        body::poll(info);
        calibration::poll();
        survey::poll();integrate::poll();gameCamera::poll();walk::poll();
        nativeShapes::poll();
        live::poll(info);
        presentation::scenePose();linkBones::probe();
        nativeStream::poll();
        if(resourcesFailed.load()&&live::base&&!live::failed)live::fault("GPU-resources");
        physical::poll();
        nativeDamage::poll();
        runeOwned::poll();memfind::poll();pouch::poll();
        saveLink::poll();
        actors::poll();
        water::poll();
        spawn::poll();
        swing::poll();actors::trackLab();labVector::poll();hudHide::poll();
        walls::poll();
        std::ifstream liveConfig(directory+"/live-render.txt");int liveEnable=0;liveConfig>>liveEnable;presentation::enabled=liveEnable==1;
        std::ifstream cmd(directory+"/E-control.txt");std::string line;std::getline(cmd,line);cmd.close();
        if(line!=last&&!line.empty()){
            last=line;int a=0;
            if(line.starts_with("interact"))a=1;else if(line.starts_with("down"))a=2;else if(line.starts_with("up"))a=3;else if(line.starts_with("cancel"))a=4;else if(line.starts_with("plus"))a=5;else if(line.starts_with("right"))a=6;else if(line.starts_with("left"))a=7;else if(line.starts_with("tilt"))a=8;else if(line.starts_with("forward"))a=9;else if(line.starts_with("tab-right"))a=10;else if(line.starts_with("tab-left"))a=11;else if(line.starts_with("weapon"))a=12;
            inputUntil.store(GetTickCount64()+2000);inputAction.store(a);fprintf(info,"input %s\n",line.c_str());fflush(info);
        }
        std::ifstream config(directory+"/render.txt");int enabled=0,overlay=0;float anchor[3];
        if(config>>enabled>>overlay>>anchor[0]>>anchor[1]>>anchor[2]){
            bool valid=true;for(float v:anchor)valid&=std::isfinite(v)&&std::abs(v)<100000;
            if(valid&&!anchorBusy.test_and_set(std::memory_order_acquire)){for(int i=0;i<3;i++)fixtureAnchor[i]=anchor[i];anchorBusy.clear(std::memory_order_release);renderEnabled.store(enabled==1);overlayEnabled.store(overlay==1);}
        }
        config.close();
        size_t t=tail.load(),h=head.load(std::memory_order_acquire);while(t<h&&eventReady[t%capacity].load(std::memory_order_acquire)){trace::control(events[t%capacity]);if(trace::room(f,directory+"/frames.jsonl",nullptr,trace::frameRotations)){fputs(events[t%capacity],f);fputc('\n',f);}eventReady[t%capacity].store(false,std::memory_order_release);++t;}tail.store(t,std::memory_order_release);if(f)fflush(f);
        static uint64_t previous=~0ULL;if(drops.load()!=previous){previous=drops.load();fprintf(info,"dropped=%llu\n",previous);fflush(info);}
        static ULONGLONG lastStats=0;
        if(GetTickCount64()-lastStats>=1000){
            trace::stats();
            lastStats=GetTickCount64();LARGE_INTEGER t;QueryPerformanceCounter(&t);
            for(size_t i=0;i<size_t(Reject::count);i++)event("{\"event\":\"composition_counter\",\"qpc\":%llu,\"reason\":\"%s\",\"count\":%llu}",(uint64_t)t.QuadPart,rejectNames[i],rejects[i].load());
            event("{\"event\":\"camera_counter\",\"qpc\":%llu,\"read_rejects\":%llu,\"publish_busy\":%llu}",(uint64_t)t.QuadPart,cameraReadRejects.load(),cameraPublishBusy.load());
            event("{\"event\":\"stats\",\"qpc\":%llu,\"full\":%d,\"body_seq\":%llu,\"body_epoch\":%llu,\"actor\":%u,\"controller\":%u,\"body\":%u,\"ready\":%d,\"screen\":%d,\"active\":%d,\"body_drops\":%llu,\"event_drops\":%llu,\"sample_drops\":%llu,\"bad_post_args\":%llu,\"bad_post_epoch\":%llu,\"expired\":%llu}",(uint64_t)t.QuadPart,int(fullEvidence.load()),body::controlEpoch.load(),body::lifecycleEpoch.load(),body::identityActor.load(),body::identityCtrl.load(),body::identityBody.load(),int(body::allowed()),body::screenReason.load(),int(body::active.load()),body::dropped.load(),drops.load(),body::sampleDrops.load(),body::badPostArgs.load(),body::badPostEpoch.load(),body::expiredObservations.load());
            for(auto pair:{std::pair{"match",&bodyMatchCpu},{"sample",&bodySampleCpu},{"claim",&bodyClaimCpu},{"post",&bodyPostCpu},{"input",&inputCpu},{"camera",&cameraCpu},{"vulkan",&vulkanCpu},{"present",&presentCpu},{"selector",&selectorCpu},{"opacity",&opacityCpu}})
                event("{\"event\":\"callback_cpu\",\"qpc\":%llu,\"callback\":\"%s\",\"calls\":%llu,\"ticks\":%llu,\"max_ticks\":%llu}",(uint64_t)t.QuadPart,pair.first,pair.second->calls.load(),pair.second->ticks.load(),pair.second->maximum.load());
            {LARGE_INTEGER frequency;QueryPerformanceFrequency(&frequency);static uint64_t frames0=0,late0=0,ticks0=0;
                uint64_t frames=paceFrames.load(),late=paceLate.load(),ticks=paceTicks.load(),worst=paceMax.exchange(0);
                uint64_t work[14];for(int i=0;i<14;i++)work[i]=workBuckets[i].exchange(0);uint64_t workSum=workTicks.exchange(0),workCount=workFrames.exchange(0);
                event("{\"event\":\"frame_pacing\",\"qpc\":%llu,\"frames\":%llu,\"late\":%llu,\"mean_ms\":%.3f,\"max_ms\":%.3f,\"total_frames\":%llu,\"total_late\":%llu,\"work_mean_ms\":%.3f,\"work\":[%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu]}",(uint64_t)t.QuadPart,frames-frames0,late-late0,frames>frames0?double(ticks-ticks0)*1000/frequency.QuadPart/double(frames-frames0):0.,double(worst)*1000/frequency.QuadPart,frames,late,
                    workCount?double(workSum)*1000/frequency.QuadPart/double(workCount):0.,work[0],work[1],work[2],work[3],work[4],work[5],work[6],work[7],work[8],work[9],work[10],work[11],work[12],work[13]);
                frames0=frames;late0=late;ticks0=ticks;
                {static uint64_t paced0=0,early0=0,held0=0;uint64_t paced=framePace::frames.load(),early=framePace::early.load(),held=framePace::held.load();
                    event("{\"event\":\"frame_pace\",\"qpc\":%llu,\"budget_ms\":%d,\"frames\":%llu,\"early\":%llu,\"held\":%llu}",(uint64_t)t.QuadPart,framePace::budgetMs.load(),paced-paced0,early-early0,held-held0);paced0=paced;early0=early;held0=held;}
                if(frameMarks::enabled.load())for(int k=0;k<frameMarks::pCount;k++){
                    char list[720];size_t used=0;for(int i=0;i<60;i++)used+=size_t(snprintf(list+used,sizeof(list)-used,"%s%u",i?",":"",frameMarks::phase[k][i].exchange(0)));
                    event("{\"event\":\"frame_phase\",\"qpc\":%llu,\"name\":\"%s\",\"ms\":[%s]}",(uint64_t)t.QuadPart,frameMarks::phaseNames[k],list);
                }}
            FILETIME created,exited,kernel,user;uint64_t threadTicks=0;
            if(GetThreadTimes(GetCurrentThread(),&created,&exited,&kernel,&user))threadTicks=(uint64_t(kernel.dwHighDateTime)<<32)+kernel.dwLowDateTime+(uint64_t(user.dwHighDateTime)<<32)+user.dwLowDateTime;
            event("{\"event\":\"observer_cpu\",\"qpc\":%llu,\"guest_calls\":%llu,\"guest_interval_ticks\":%llu,\"guest_interval_max_ticks\":%llu,\"worker_cpu_100ns\":%llu}",(uint64_t)t.QuadPart,body::observerCalls.load(),body::observerTicks.load(),body::observerMaxTicks.load(),threadTicks);
        }
        // The next region is asked for as soon as a harvest has ended, so that one is read in every
        // game frame (a fixed 20 ms sleep left a third of the frames without a request: 24 reads/s, route-60).
        WaitForSingleObject(nativeShapes::harvested,20);
    }
}

// Observation first: marker clears are consumed, native pixels/depth remain intact.
struct ImageInfo{VkImageCreateInfo info;uint64_t generation;VkImageLayout layout;};
static std::unordered_map<VkImage,ImageInfo> images;
static std::mutex imageMutex;
static uint64_t imageGeneration=0,presents=0;
struct ColorMarker{VkImage image;uint32_t frame;uint64_t generation,present;};
static std::unordered_map<VkCommandBuffer,std::vector<ColorMarker>> colorMarkers;
// The guest scene extent excludes later overlapping Cemu surface aliases.
// A resolution pack scales the host image and leaves the guest extent alone
// (1280x720 guest, 1920x1080 host with the Graphics pack), so the host image
// must be an exact multiple of the guest extent in the same proportion; the
// colour and depth images must then agree with each other exactly.
// Ambiguous candidates fail closed; never choose an arbitrary last marker.
static bool matchingSceneExtent(const ImageInfo& image,const Camera& camera){
    const auto& e=image.info.extent;
    return camera.sceneW>=1024&&camera.sceneH>=512&&e.width>=camera.sceneW/4&&uint64_t(e.width)*camera.sceneH==uint64_t(e.height)*camera.sceneW&&image.info.arrayLayers==1;
}
static bool chooseSceneMarker(const std::vector<ColorMarker>& candidates,const Camera& camera,ColorMarker& result){
    if(candidates.size()>8)return false;
    bool found=false;
    for(auto& candidate:candidates){auto i=images.find(candidate.image);
        if(candidate.frame!=camera.id||candidate.present!=presents||i==images.end()||i->second.generation!=candidate.generation||!matchingSceneExtent(i->second,camera))continue;
        if(found)return false;
        result=candidate;found=true;
    }return found;
}
static std::unordered_map<VkImageView,VkImage> views;
static std::unordered_map<VkFramebuffer,std::vector<VkImage>> framebuffers;
static std::unordered_map<VkCommandBuffer,VkFramebuffer> currentFramebuffers;
// Ancestry hints are recording evidence, not pixel visibility or GPU completion.
struct OutputHint {uint32_t frame=0;uint64_t present=0,epoch=0,recording=0;VkImage origin=VK_NULL_HANDLE;};
static std::unordered_map<VkImage,OutputHint> outputHints;
static std::unordered_map<VkSwapchainKHR,std::vector<VkImage>> swapImages;
static std::unordered_map<VkDescriptorSet,std::unordered_map<uint64_t,VkImageView>> descriptorImages;
static std::unordered_map<VkCommandBuffer,uint64_t> recordings;
static std::unordered_map<VkCommandBuffer,std::vector<uint32_t>> recordedDraws;
static uint64_t nextRecording=0,submissions=0;
static void outputDraw(VkCommandBuffer cb,uint32_t vertices){
    // Pinned Cemu output/postprocess quads have <=6 vertices. Avoid inspecting
    // every world draw. This intentionally cannot prove arbitrary shader paths.
    if(vertices>6)return;
    auto fb=currentFramebuffers.find(cb);if(fb==currentFramebuffers.end())return;
    std::lock_guard lk(imageMutex);
    auto attachments=framebuffers.find(fb->second);if(attachments==framebuffers.end())return;
    OutputHint hint{};VkImage source=VK_NULL_HANDLE;
    auto& native=nativeState(cb);
    for(auto& slot:native.slots){
        auto ds=slot.set?descriptorImages.find(slot.set):descriptorImages.end();if(ds==descriptorImages.end())continue;
        for(auto& [key,view]:ds->second){
            auto vi=views.find(view);if(vi==views.end())continue;
            auto h=outputHints.find(vi->second);
            if(h!=outputHints.end()&&h->second.present==presents&&h->second.epoch==targetEpoch.load()){
                hint=h->second;source=vi->second;
            }
        }
    }
    if(!source)return;
    for(auto image:attachments->second){
        auto im=images.find(image);bool isSwap=false;
        for(auto& [swap,list]:swapImages)if(std::find(list.begin(),list.end(),image)!=list.end())isSwap=true;
        if(!isSwap&&(im==images.end()||im->second.info.extent.width<1024||im->second.info.extent.height<512||im->second.info.format==VK_FORMAT_D32_SFLOAT))continue;
        outputHints[image]=hint;
        event("{\"event\":\"output_sample\",\"frame\":%u,\"present\":%llu,\"cb\":%llu,\"recording\":%llu,\"source\":%llu,\"destination\":%llu,\"origin\":%llu,\"swap_image\":%d}",hint.frame,presents,(uint64_t)cb,recordings[cb],(uint64_t)source,(uint64_t)image,(uint64_t)hint.origin,int(isSwap));
    }
}
#include "frame_survey.h"
#include "integrate.h"
struct InstanceOverrides{};
struct DeviceOverrides {
    static void DestroyDevice(const vkroots::VkDeviceDispatch& d,VkDevice dev,const VkAllocationCallbacks* a){ CpuScope timing(vulkanCpu);
        physical::stop("device-shutdown");
        // Shutdown only; never waits in the ordinary draw path.
        d.DeviceWaitIdle(dev);clearGpu(d);survey::destroy(d);integrate::destroy(d);++targetEpoch;resourcesFailed=true;
        while(!targets.empty())retireTargets(d,targets.back().color);
        if(meshPipeline)d.DestroyPipeline(dev,meshPipeline,nullptr);
        if(overlayPipeline)d.DestroyPipeline(dev,overlayPipeline,nullptr);
        if(blendCullPipeline)d.DestroyPipeline(dev,blendCullPipeline,nullptr);
        if(blendPipeline)d.DestroyPipeline(dev,blendPipeline,nullptr);
        if(renderPass)d.DestroyRenderPass(dev,renderPass,nullptr);
        if(pipelineLayout)d.DestroyPipelineLayout(dev,pipelineLayout,nullptr);
        if(descriptorPool)d.DestroyDescriptorPool(dev,descriptorPool,nullptr);
        if(descriptorLayout)d.DestroyDescriptorSetLayout(dev,descriptorLayout,nullptr);
        if(sampler)d.DestroySampler(dev,sampler,nullptr);
        if(timingPool)d.DestroyQueryPool(dev,timingPool,nullptr);
        for(auto* t:{&atlasTexture,&overlayTexture}){if(t->view)d.DestroyImageView(dev,t->view,nullptr);if(t->image)d.DestroyImage(dev,t->image,nullptr);if(t->memory)d.FreeMemory(dev,t->memory,nullptr);if(t->staging)d.DestroyBuffer(dev,t->staging,nullptr);if(t->stagingMemory)d.FreeMemory(dev,t->stagingMemory,nullptr);}
        if(vertexBuffer)d.DestroyBuffer(dev,vertexBuffer,nullptr);
        if(vertexMemory)d.FreeMemory(dev,vertexMemory,nullptr);
        d.DestroyDevice(dev,a);
    }
    static void DestroySwapchainKHR(const vkroots::VkDeviceDispatch& d,VkDevice dev,VkSwapchainKHR swapchain,const VkAllocationCallbacks* a){ CpuScope timing(vulkanCpu);
        // Pinned Cemu RecreateSwapchain submits then WaitDeviceIdle before Cleanup.
        // This is a Cemu-specific quiescent point, not a generic layer guarantee.
        ++targetEpoch;while(!targets.empty())retireTargets(d,targets.back().color);colorMarkers.clear();
        {std::lock_guard lk(imageMutex);swapImages.erase(swapchain);outputHints.clear();}
        event("{\"event\":\"swapchain_retire\",\"swapchain\":%llu,\"epoch\":%llu}",(uint64_t)swapchain,targetEpoch.load());d.DestroySwapchainKHR(dev,swapchain,a);
    }
    static void GetDeviceQueue(const vkroots::VkDeviceDispatch& d,VkDevice dev,uint32_t family,uint32_t index,VkQueue* out){ CpuScope timing(vulkanCpu);
        uint32_t count=0;auto& pd=*d.pPhysicalDeviceDispatch;pd.GetPhysicalDeviceQueueFamilyProperties(d.PhysicalDevice,&count,nullptr);std::vector<VkQueueFamilyProperties> props(count);pd.GetPhysicalDeviceQueueFamilyProperties(d.PhysicalDevice,&count,props.data());if(family<count)timestampBits=props[family].timestampValidBits;event("{\"event\":\"queue\",\"family\":%u,\"index\":%u,\"timestamp_bits\":%u}",family,index,timestampBits);d.GetDeviceQueue(dev,family,index,out);
    }
    // The per-draw hooks below are not timed: two clock reads on each of about ten thousand calls a frame cost more than the bookkeeping.
    static void CmdPipelineBarrier(const vkroots::VkCommandBufferDispatch& d,VkCommandBuffer cb,VkPipelineStageFlags src,VkPipelineStageFlags dst,VkDependencyFlags dep,uint32_t mc,const VkMemoryBarrier* m,uint32_t bc,const VkBufferMemoryBarrier* b,uint32_t ic,const VkImageMemoryBarrier* ims){
        if(ic){std::lock_guard lk(imageMutex);for(uint32_t i=0;i<ic;i++)if(ims[i].subresourceRange.baseMipLevel==0&&ims[i].subresourceRange.baseArrayLayer==0){auto it=images.find(ims[i].image);if(it!=images.end())it->second.layout=ims[i].newLayout;}}
        d.CmdPipelineBarrier(cb,src,dst,dep,mc,m,bc,b,ic,ims);
    }
    static VkResult BeginCommandBuffer(const vkroots::VkCommandBufferDispatch& d,VkCommandBuffer cb,const VkCommandBufferBeginInfo* ci){ gpuRecorded.erase(cb);nativeState(cb).clear();colorMarkers.erase(cb);currentFramebuffers.erase(cb);recordings[cb]=++nextRecording;recordedDraws[cb].clear();return d.BeginCommandBuffer(cb,ci);}
    static void UpdateDescriptorSets(const vkroots::VkDeviceDispatch& d,VkDevice dev,uint32_t wc,const VkWriteDescriptorSet* writes,uint32_t cc,const VkCopyDescriptorSet* copies){
        {std::lock_guard lk(imageMutex);for(uint32_t i=0;i<wc;i++)if(writes[i].pImageInfo&&(writes[i].descriptorType==VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER||writes[i].descriptorType==VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE))for(uint32_t j=0;j<writes[i].descriptorCount;j++)descriptorImages[writes[i].dstSet][(uint64_t(writes[i].dstBinding)<<32)+(writes[i].dstArrayElement+j)]=writes[i].pImageInfo[j].imageView;
            else if(writes[i].pBufferInfo&&(writes[i].descriptorType==VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC||writes[i].descriptorType==VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER))for(uint32_t j=0;j<writes[i].descriptorCount;j++){auto& known=survey::uniforms[writes[i].dstSet];known[writes[i].dstBinding+j]={writes[i].pBufferInfo[j].buffer,writes[i].pBufferInfo[j].offset,writes[i].pBufferInfo[j].range};dynamicCounts[writes[i].dstSet]=uint32_t(known.size());}}
        // Copy descriptors are not used by the pinned output path. Report rather
        // than inventing ancestry for an unobserved layout rollover.
        if(cc)event("{\"event\":\"descriptor_copy_untracked\",\"count\":%u}",cc);
        d.UpdateDescriptorSets(dev,wc,writes,cc,copies);
    }
    static VkResult FreeDescriptorSets(const vkroots::VkDeviceDispatch& d,VkDevice dev,VkDescriptorPool pool,uint32_t count,const VkDescriptorSet* sets){
        {std::lock_guard lk(imageMutex);for(uint32_t i=0;i<count;i++){descriptorImages.erase(sets[i]);survey::uniforms.erase(sets[i]);dynamicCounts.erase(sets[i]);}}return d.FreeDescriptorSets(dev,pool,count,sets);
    }
    static void CmdDraw(const vkroots::VkCommandBufferDispatch& d,VkCommandBuffer cb,uint32_t vc,uint32_t ic,uint32_t fv,uint32_t fi){if(integrate::frame.watching)integrate::nativeDraw(cb);if(survey::on())survey::draw(cb,vc,ic,false);d.CmdDraw(cb,vc,ic,fv,fi);outputDraw(cb,vc);}
    static void CmdDrawIndexed(const vkroots::VkCommandBufferDispatch& d,VkCommandBuffer cb,uint32_t count,uint32_t instances,uint32_t first,int32_t offset,uint32_t instance){if(integrate::frame.watching)integrate::nativeDraw(cb);if(survey::on())survey::draw(cb,count,instances,true);d.CmdDrawIndexed(cb,count,instances,first,offset,instance);outputDraw(cb,count);}
    static VkResult GetSwapchainImagesKHR(const vkroots::VkDeviceDispatch& d,VkDevice dev,VkSwapchainKHR swap,uint32_t* count,VkImage* out){
        auto result=d.GetSwapchainImagesKHR(dev,swap,count,out);if(result==VK_SUCCESS&&out){std::lock_guard lk(imageMutex);swapImages[swap].assign(out,out+*count);}return result;
    }
    static VkResult QueueSubmit(const vkroots::VkQueueDispatch& d,VkQueue queue,uint32_t count,const VkSubmitInfo* info,VkFence fence){
        auto serial=++submissions;frameMarks::tally(frameMarks::pSubmit);if(survey::on())survey::submitted(serial,count,info);
        for(uint32_t i=0;i<count;i++)for(uint32_t j=0;j<info[i].commandBufferCount;j++){
            auto cb=info[i].pCommandBuffers[j];for(auto frame:recordedDraws[cb])event("{\"event\":\"submit_draw\",\"submission\":%llu,\"queue\":%llu,\"cb\":%llu,\"recording\":%llu,\"frame\":%u,\"fence\":%llu}",serial,(uint64_t)queue,(uint64_t)cb,recordings[cb],frame,(uint64_t)fence);
        }
        auto result=d.QueueSubmit(queue,count,info,fence);event("{\"event\":\"submit_result\",\"submission\":%llu,\"present\":%llu,\"result\":%d}",serial,presents,result);if(result==VK_SUCCESS)finishGpuSubmit(d,queue,count,info,serial);return result;
    }
    static void CmdBindPipeline(const vkroots::VkCommandBufferDispatch& d,VkCommandBuffer cb,VkPipelineBindPoint point,VkPipeline p){if(point==VK_PIPELINE_BIND_POINT_GRAPHICS)nativeState(cb).pipeline=p;d.CmdBindPipeline(cb,point,p);}
    static void CmdBindDescriptorSets(const vkroots::VkCommandBufferDispatch& d,VkCommandBuffer cb,VkPipelineBindPoint point,VkPipelineLayout layout,uint32_t first,uint32_t count,const VkDescriptorSet* sets,uint32_t n,const uint32_t* offsets){
        if(point==VK_PIPELINE_BIND_POINT_GRAPHICS)nativeState(cb).bind(layout,first,count,sets,n,offsets);
        d.CmdBindDescriptorSets(cb,point,layout,first,count,sets,n,offsets);
    }
    static void CmdPushConstants(const vkroots::VkCommandBufferDispatch& d,VkCommandBuffer cb,VkPipelineLayout layout,VkShaderStageFlags stages,uint32_t offset,uint32_t size,const void* data){
        auto& s=nativeState(cb);std::erase_if(s.constants,[&](auto& p){return (p.stages&stages)!=0;});
        NativeState::Constants p{layout,stages,offset};p.bytes.assign(static_cast<const uint8_t*>(data),static_cast<const uint8_t*>(data)+size);s.constants.push_back(std::move(p));d.CmdPushConstants(cb,layout,stages,offset,size,data);
    }
    static void CmdBindVertexBuffers(const vkroots::VkCommandBufferDispatch& d,VkCommandBuffer cb,uint32_t first,uint32_t count,const VkBuffer* buffers,const VkDeviceSize* offsets){if(first==0&&count){auto& s=nativeState(cb);s.vertex=buffers[0];s.vertexOffset=offsets[0];}d.CmdBindVertexBuffers(cb,first,count,buffers,offsets);}
    static void CmdSetViewport(const vkroots::VkCommandBufferDispatch& d,VkCommandBuffer cb,uint32_t first,uint32_t count,const VkViewport* v){if(first==0&&count){auto& s=nativeState(cb);s.viewport=v[0];s.hasViewport=true;}d.CmdSetViewport(cb,first,count,v);}
    static void CmdSetDepthBias(const vkroots::VkCommandBufferDispatch& d,VkCommandBuffer cb,float constant,float clamp,float slope){auto& s=nativeState(cb);s.bias[0]=constant;s.bias[1]=clamp;s.bias[2]=slope;s.hasBias=true;d.CmdSetDepthBias(cb,constant,clamp,slope);}
    static void CmdSetBlendConstants(const vkroots::VkCommandBufferDispatch& d,VkCommandBuffer cb,const float constants[4]){auto& s=nativeState(cb);std::copy_n(constants,4,s.blend);s.hasBlend=true;d.CmdSetBlendConstants(cb,constants);}
    static void CmdSetScissor(const vkroots::VkCommandBufferDispatch& d,VkCommandBuffer cb,uint32_t first,uint32_t count,const VkRect2D* v){if(first==0&&count){auto& s=nativeState(cb);s.scissor=v[0];s.hasScissor=true;}d.CmdSetScissor(cb,first,count,v);}
    static VkResult CreateImageView(const vkroots::VkDeviceDispatch& d,VkDevice dev,const VkImageViewCreateInfo* ci,const VkAllocationCallbacks* a,VkImageView* out){ CpuScope timing(vulkanCpu);
        auto r=d.CreateImageView(dev,ci,a,out);if(r==VK_SUCCESS){std::lock_guard lk(imageMutex);views[*out]=ci->image;survey::viewRanges[*out]={ci->subresourceRange.baseMipLevel,ci->subresourceRange.baseArrayLayer,ci->subresourceRange.layerCount,uint32_t(ci->format)};}return r;
    }
    static void DestroyImageView(const vkroots::VkDeviceDispatch& d,VkDevice dev,VkImageView v,const VkAllocationCallbacks* a){ CpuScope timing(vulkanCpu);
        {std::lock_guard lk(imageMutex);views.erase(v);survey::viewRanges.erase(v);}d.DestroyImageView(dev,v,a);
    }
    static VkResult CreateFramebuffer(const vkroots::VkDeviceDispatch& d,VkDevice dev,const VkFramebufferCreateInfo* ci,const VkAllocationCallbacks* a,VkFramebuffer* out){ CpuScope timing(vulkanCpu);
        auto r=d.CreateFramebuffer(dev,ci,a,out);if(r==VK_SUCCESS){std::lock_guard lk(imageMutex);auto& ims=framebuffers[*out];auto& ranges=survey::framebufferRanges[*out];for(uint32_t i=0;i<ci->attachmentCount;i++){ims.push_back(views[ci->pAttachments[i]]);ranges.push_back(survey::viewRanges[ci->pAttachments[i]]);}}return r;
    }
    static void DestroyFramebuffer(const vkroots::VkDeviceDispatch& d,VkDevice dev,VkFramebuffer f,const VkAllocationCallbacks* a){ CpuScope timing(vulkanCpu);
        {std::lock_guard lk(imageMutex);framebuffers.erase(f);survey::framebufferRanges.erase(f);}integrate::kinds.erase(f);d.DestroyFramebuffer(dev,f,a);
    }
    static void CmdBeginRenderPass(const vkroots::VkCommandBufferDispatch& d,VkCommandBuffer cb,const VkRenderPassBeginInfo* p,VkSubpassContents c){
        integrate::beforePass(d,cb,p->framebuffer); // may record passes of ours first: none is open here
        if(survey::state.load(std::memory_order_relaxed)!=survey::kIdle)survey::beforePass(d,cb,p);
        currentFramebuffers[cb]=p->framebuffer;d.CmdBeginRenderPass(cb,p,c);frameMarks::tally(frameMarks::pPass);if(survey::on())survey::passBegin(cb,p);
    }
    static void CmdEndRenderPass(const vkroots::VkCommandBufferDispatch& d,VkCommandBuffer cb){currentFramebuffers.erase(cb);d.CmdEndRenderPass(cb);if(survey::on())survey::passEnd(cb);}
    // The frame survey's tables and its record of copies between targets (frame_survey.h).
    static VkResult CreateShaderModule(const vkroots::VkDeviceDispatch& d,VkDevice dev,const VkShaderModuleCreateInfo* ci,const VkAllocationCallbacks* a,VkShaderModule* out){
        auto r=d.CreateShaderModule(dev,ci,a,out);if(r==VK_SUCCESS)survey::shaderCreated(*out,ci);return r;
    }
    static void DestroyShaderModule(const vkroots::VkDeviceDispatch& d,VkDevice dev,VkShaderModule m,const VkAllocationCallbacks* a){{std::lock_guard lk(survey::tables);survey::shaders.erase(m);}d.DestroyShaderModule(dev,m,a);}
    static VkResult CreateGraphicsPipelines(const vkroots::VkDeviceDispatch& d,VkDevice dev,VkPipelineCache cache,uint32_t count,const VkGraphicsPipelineCreateInfo* ci,const VkAllocationCallbacks* a,VkPipeline* out){
        auto r=d.CreateGraphicsPipelines(dev,cache,count,ci,a,out);if(r==VK_SUCCESS)for(uint32_t i=0;i<count;i++)if(out[i])survey::pipelineCreated(out[i],ci[i]);return r;
    }
    static void DestroyPipeline(const vkroots::VkDeviceDispatch& d,VkDevice dev,VkPipeline p,const VkAllocationCallbacks* a){{std::lock_guard lk(survey::tables);survey::pipes.erase(p);}d.DestroyPipeline(dev,p,a);}
    static VkResult CreateRenderPass(const vkroots::VkDeviceDispatch& d,VkDevice dev,const VkRenderPassCreateInfo* ci,const VkAllocationCallbacks* a,VkRenderPass* out){
        auto r=d.CreateRenderPass(dev,ci,a,out);if(r==VK_SUCCESS)survey::renderPassCreated(*out,ci);return r;
    }
    static void DestroyRenderPass(const vkroots::VkDeviceDispatch& d,VkDevice dev,VkRenderPass p,const VkAllocationCallbacks* a){{std::lock_guard lk(survey::tables);survey::renderPasses.erase(p);}d.DestroyRenderPass(dev,p,a);}
    static VkResult AllocateMemory(const vkroots::VkDeviceDispatch& d,VkDevice dev,const VkMemoryAllocateInfo* ai,const VkAllocationCallbacks* a,VkDeviceMemory* out){
        auto r=d.AllocateMemory(dev,ai,a,out);if(r==VK_SUCCESS)survey::memoryAllocated(*out,ai);return r;
    }
    static void FreeMemory(const vkroots::VkDeviceDispatch& d,VkDevice dev,VkDeviceMemory m,const VkAllocationCallbacks* a){{std::lock_guard lk(survey::tables);survey::memories.erase(m);}d.FreeMemory(dev,m,a);}
    static VkResult MapMemory(const vkroots::VkDeviceDispatch& d,VkDevice dev,VkDeviceMemory m,VkDeviceSize offset,VkDeviceSize size,VkMemoryMapFlags flags,void** out){
        auto r=d.MapMemory(dev,m,offset,size,flags,out);if(r==VK_SUCCESS)survey::memoryMapped(m,offset,*out);return r;
    }
    static void UnmapMemory(const vkroots::VkDeviceDispatch& d,VkDevice dev,VkDeviceMemory m){survey::memoryUnmapped(m);d.UnmapMemory(dev,m);}
    static VkResult BindBufferMemory(const vkroots::VkDeviceDispatch& d,VkDevice dev,VkBuffer b,VkDeviceMemory m,VkDeviceSize offset){
        auto r=d.BindBufferMemory(dev,b,m,offset);if(r==VK_SUCCESS){std::lock_guard lk(survey::tables);survey::buffers[b]={m,offset};}return r;
    }
    static void DestroyBuffer(const vkroots::VkDeviceDispatch& d,VkDevice dev,VkBuffer b,const VkAllocationCallbacks* a){{std::lock_guard lk(survey::tables);survey::buffers.erase(b);}d.DestroyBuffer(dev,b,a);}
    static void CmdCopyImage(const vkroots::VkCommandBufferDispatch& d,VkCommandBuffer cb,VkImage from,VkImageLayout fl,VkImage to,VkImageLayout tl,uint32_t n,const VkImageCopy* regions){
        if(survey::on())survey::transfer("copy",from,to,n,n?&regions[0].srcSubresource:nullptr,n?&regions[0].dstSubresource:nullptr);d.CmdCopyImage(cb,from,fl,to,tl,n,regions);
    }
    static void CmdBlitImage(const vkroots::VkCommandBufferDispatch& d,VkCommandBuffer cb,VkImage from,VkImageLayout fl,VkImage to,VkImageLayout tl,uint32_t n,const VkImageBlit* regions,VkFilter filter){
        if(survey::on())survey::transfer("blit",from,to,n,n?&regions[0].srcSubresource:nullptr,n?&regions[0].dstSubresource:nullptr);d.CmdBlitImage(cb,from,fl,to,tl,n,regions,filter);
    }
    static void CmdCopyBufferToImage(const vkroots::VkCommandBufferDispatch& d,VkCommandBuffer cb,VkBuffer from,VkImage to,VkImageLayout tl,uint32_t n,const VkBufferImageCopy* regions){
        if(survey::on())survey::transfer("upload",VK_NULL_HANDLE,to,n,nullptr,n?&regions[0].imageSubresource:nullptr);d.CmdCopyBufferToImage(cb,from,to,tl,n,regions);
    }
    static void CmdCopyImageToBuffer(const vkroots::VkCommandBufferDispatch& d,VkCommandBuffer cb,VkImage from,VkImageLayout fl,VkBuffer to,uint32_t n,const VkBufferImageCopy* regions){
        if(survey::on())survey::transfer("readback",from,VK_NULL_HANDLE,n,n?&regions[0].imageSubresource:nullptr,nullptr);d.CmdCopyImageToBuffer(cb,from,fl,to,n,regions);
    }
    static void CmdClearAttachments(const vkroots::VkCommandBufferDispatch& d,VkCommandBuffer cb,uint32_t count,const VkClearAttachment* a,uint32_t rectCount,const VkClearRect* rects){ CpuScope timing(vulkanCpu);
        for(uint32_t i=0;i<count;i++)if(a[i].aspectMask&VK_IMAGE_ASPECT_COLOR_BIT){
            auto v=a[i].clearValue.color.float32;
            if(std::abs(v[1]-.123456789f)<1e-6f&&std::abs(v[2]-.987654321f)<1e-6f){
                VkImage im=VK_NULL_HANDLE;{std::lock_guard lk(imageMutex);auto it=framebuffers.find(currentFramebuffers[cb]);if(it!=framebuffers.end()&&a[i].colorAttachment<it->second.size())im=it->second[a[i].colorAttachment];}
                event("{\"event\":\"attachment_marker\",\"frame\":%u,\"present\":%llu,\"cb\":%llu,\"image\":%llu,\"count\":%u}",markerFrame(v[3]),presents,(uint64_t)cb,(uint64_t)im,count);return;
            }
        }
        if(survey::on()){std::lock_guard lk(imageMutex);auto it=framebuffers.find(currentFramebuffers[cb]);
            for(uint32_t i=0;i<count;i++){bool colour=a[i].aspectMask&VK_IMAGE_ASPECT_COLOR_BIT;VkImage im=VK_NULL_HANDLE;if(it!=framebuffers.end()&&colour&&a[i].colorAttachment<it->second.size())im=it->second[a[i].colorAttachment];
                survey::cleared(colour?"attachment-colour":"attachment-depth",im,cb,colour?a[i].clearValue.color.float32:nullptr,colour?0:a[i].clearValue.depthStencil.depth);}}
        d.CmdClearAttachments(cb,count,a,rectCount,rects);
    }
    static VkResult CreateImage(const vkroots::VkDeviceDispatch& d,VkDevice dev,const VkImageCreateInfo* ci,const VkAllocationCallbacks* a,VkImage* out){ CpuScope timing(vulkanCpu);
        auto r=d.CreateImage(dev,ci,a,out);
        if(r==VK_SUCCESS){std::lock_guard lk(imageMutex);images[*out]={*ci,++imageGeneration,ci->initialLayout};images[*out].info.pNext=nullptr;
            if(ci->extent.width>=1024&&ci->extent.height>=512)event("{\"event\":\"image_create\",\"image\":%llu,\"generation\":%llu,\"format\":%d,\"width\":%u,\"height\":%u,\"samples\":%u,\"usage\":%u,\"initialLayout\":%d}",(uint64_t)*out,imageGeneration,ci->format,ci->extent.width,ci->extent.height,ci->samples,ci->usage,ci->initialLayout);}
        return r;
    }
    static void DestroyImage(const vkroots::VkDeviceDispatch& d,VkDevice dev,VkImage im,const VkAllocationCallbacks* a){ CpuScope timing(vulkanCpu);
        retireTargets(d,im);integrate::retire(d,im);
        {std::lock_guard lk(imageMutex);auto it=images.find(im);if(it!=images.end()){if(it->second.info.extent.width>=1024)event("{\"event\":\"image_destroy\",\"image\":%llu,\"generation\":%llu}",(uint64_t)im,it->second.generation);images.erase(it);}}
        {std::lock_guard lk(imageMutex);outputHints.erase(im);}d.DestroyImage(dev,im,a);
    }
    static void CmdClearColorImage(const vkroots::VkCommandBufferDispatch& d,VkCommandBuffer cb,VkImage im,VkImageLayout l,const VkClearColorValue* v,uint32_t n,const VkImageSubresourceRange* ranges){ CpuScope timing(vulkanCpu);
        if(std::abs(v->float32[1]-.121568628f)<1e-6f&&std::abs(v->float32[2]-.984313726f)<1e-6f){
            {std::lock_guard lk(imageMutex);auto it=images.find(im);if(it!=images.end()&&it->second.info.format==VK_FORMAT_A2B10G10R10_UNORM_PACK32&&it->second.info.samples==VK_SAMPLE_COUNT_1_BIT&&n==1&&ranges[0].baseMipLevel==0&&ranges[0].baseArrayLayer==0){
                auto& list=colorMarkers[cb];uint32_t frame=markerFrame(v->float32[3]);
                if(!list.empty()&&(list.front().frame!=frame||list.front().present!=presents))list.clear();
                if(list.size()<=8&&std::none_of(list.begin(),list.end(),[&](auto& m){return m.image==im;}))list.push_back({im,frame,it->second.generation,presents});
            }}
            event("{\"event\":\"color_marker\",\"frame\":%u,\"present\":%llu,\"cb\":%llu,\"image\":%llu,\"layout\":%d,\"ranges\":%u}",markerFrame(v->float32[3]),presents,(uint64_t)cb,(uint64_t)im,l,n);return;
        }{std::lock_guard lk(imageMutex);outputHints.erase(im);}if(survey::on())survey::cleared("colour",im,cb,v->float32,0);d.CmdClearColorImage(cb,im,l,v,n,ranges);
    }
    static void CmdClearDepthStencilImage(const vkroots::VkCommandBufferDispatch& d,VkCommandBuffer cb,VkImage im,VkImageLayout l,const VkClearDepthStencilValue* v,uint32_t n,const VkImageSubresourceRange* ranges){ CpuScope timing(vulkanCpu);
        if(std::abs(v->depth-.0123456789f)<1e-7f){
            event("{\"event\":\"depth_marker\",\"frame\":%u,\"present\":%llu,\"cb\":%llu,\"image\":%llu,\"layout\":%d,\"ranges\":%u}",v->stencil,presents,(uint64_t)cb,(uint64_t)im,l,n);
            auto marker=colorMarkers.find(cb);Reject reason=Reject::draw;
            uint32_t frame=marker==colorMarkers.end()||marker->second.empty()?0:marker->second.front().frame;
            ImageInfo depthInfo{},colorInfo{};Camera camera{};ColorMarker chosen{};
            if(!frame)reason=Reject::noMarker;
            else if((frame&255)!=v->stencil)reason=Reject::markerFrame;
            else if(marker->second.front().present!=presents)reason=Reject::markerPresent;
            else if(l!=VK_IMAGE_LAYOUT_GENERAL||n!=1||ranges[0].baseMipLevel!=0||ranges[0].baseArrayLayer!=0)reason=Reject::markerRange;
            if(reason==Reject::draw){
                if(cameraBusy.test_and_set(std::memory_order_acquire))reason=Reject::cameraBusy;
                else{camera=cameras[frame%256];cameraBusy.clear(std::memory_order_release);}
            }
            if(reason==Reject::draw&&camera.id!=frame)reason=Reject::cameraFrame;
            if(reason==Reject::draw){
                std::lock_guard lk(imageMutex);
                if(!chooseSceneMarker(marker->second,camera,chosen))reason=Reject::imageExtent;
                auto depth=images.find(im),color=images.find(chosen.image);
                if(depth==images.end()||color==images.end())reason=Reject::missingImage;
                else{
                    depthInfo=depth->second;colorInfo=color->second;
                    if(depthInfo.info.format!=VK_FORMAT_D32_SFLOAT||depthInfo.info.samples!=VK_SAMPLE_COUNT_1_BIT)reason=Reject::imageFormat;
                    else if(!matchingSceneExtent(depthInfo,camera)||!matchingSceneExtent(colorInfo,camera)||depthInfo.info.extent.width!=colorInfo.info.extent.width||depthInfo.info.extent.height!=colorInfo.info.extent.height)reason=Reject::imageExtent;
                    else if(depthInfo.layout!=VK_IMAGE_LAYOUT_GENERAL||colorInfo.layout!=VK_IMAGE_LAYOUT_GENERAL)reason=Reject::imageLayout;
                    else if(currentFramebuffers.find(cb)!=currentFramebuffers.end())reason=Reject::activePass;
                }
            }
            if(reason==Reject::draw){
                if(camera.id!=frame)reason=Reject::cameraFrame;
                else if(camera.epoch!=targetEpoch.load())reason=Reject::cameraEpoch;
                else if(!sceneProjection(camera))reason=Reject::projection;
                else if(!camera.gameplay)reason=Reject::gameplay;
                else if(camera.bodyEpoch!=body::lifecycleEpoch.load())reason=Reject::bodyEpoch;
            }
            // What integrate.h has already put into this frame is not drawn again here.
            int left=frame?integrate::markerParts(frame,reason==Reject::draw?&camera:nullptr):(integrate::presented(),3);
            if(reason==Reject::draw){
                reason=drawFixture(d,cb,chosen.image,im,colorInfo.generation,depthInfo.generation,{depthInfo.info.extent.width,depthInfo.info.extent.height},camera,left);
                if(reason==Reject::draw){
                    hostSceneW=depthInfo.info.extent.width;hostSceneH=depthInfo.info.extent.height;
                    recordedDraws[cb].push_back(frame);std::lock_guard lk(imageMutex);
                    outputHints[chosen.image]={frame,presents,targetEpoch.load(),recordings[cb],chosen.image};
                }
                colorMarkers.erase(marker);
            }
            ++rejects[size_t(reason)];LARGE_INTEGER now;QueryPerformanceCounter(&now);
            if(survey::on())survey::marker(camera,chosen.image,im,rejectNames[size_t(reason)]);
            event("{\"event\":\"composition\",\"qpc\":%llu,\"frame\":%u,\"present\":%llu,\"cb\":%llu,\"recording\":%llu,\"reason\":\"%s\",\"near\":%.9g,\"far\":%.9g,\"camera_epoch\":%llu,\"target_epoch\":%llu,\"color_generation\":%llu,\"depth_generation\":%llu}",(uint64_t)now.QuadPart,frame,presents,(uint64_t)cb,recordings[cb],rejectNames[size_t(reason)],camera.nearZ,camera.farZ,camera.epoch,targetEpoch.load(),colorInfo.generation,depthInfo.generation);
            return;
        }
        if(survey::on())survey::cleared("depth",im,cb,nullptr,v->depth);
        d.CmdClearDepthStencilImage(cb,im,l,v,n,ranges);
    }
    static VkResult QueuePresentKHR(const vkroots::VkQueueDispatch& d,VkQueue q,const VkPresentInfoKHR* p){ CpuScope timing(presentCpu);
        auto result=d.QueuePresentKHR(q,p);LARGE_INTEGER now;QueryPerformanceCounter(&now);
        // Frame pacing as presented. The game waits for its own 60 Hz vertical
        // blank, so a frame that misses 33.3 ms is shown after 50: "late".
        {static uint64_t last=0;static LARGE_INTEGER frequency{};if(!frequency.QuadPart)QueryPerformanceFrequency(&frequency);
            uint64_t interval=last?uint64_t(now.QuadPart)-last:0;
            if(!last)last=now.QuadPart;
            else if(interval*200>=uint64_t(frequency.QuadPart)){ // a second present within 5 ms belongs to the same frame
                last=now.QuadPart;++paceFrames;paceTicks.fetch_add(interval);if(interval*25>uint64_t(frequency.QuadPart))++paceLate;
                int64_t began=frameStartQpc.exchange(0,std::memory_order_relaxed);
                if(began&&now.QuadPart>began){uint64_t work=uint64_t(now.QuadPart-began);workTicks.fetch_add(work);++workFrames;++workBuckets[std::min<uint64_t>(13,work*1000/uint64_t(frequency.QuadPart)/3)];}
                uint64_t m=paceMax.load();while(interval>m&&!paceMax.compare_exchange_weak(m,interval)){}
                frameMarks::presentQpc.store(now.QuadPart,std::memory_order_relaxed);
                if(frameMarks::enabled.load()){
                    // Milliseconds after the previous present; -1: did not run in this frame.
                    double ms[frameMarks::kCount];uint32_t tid[frameMarks::kCount];int64_t start=now.QuadPart-int64_t(interval);
                    for(int k=0;k<frameMarks::kCount;k++){int64_t t=frameMarks::at[k].exchange(0);tid[k]=frameMarks::thread[k].load();ms[k]=t?double(t-start)*1000/frequency.QuadPart:-1;}
                    event("{\"event\":\"frame_timeline\",\"qpc\":%llu,\"interval_ms\":%.3f,\"t_input\":%.3f,\"t_camera\":%.3f,\"t_sample\":%.3f,\"t_post\":%.3f,\"t_harvest\":%.3f,\"t_harvest_end\":%.3f,\"t_draw\":%.3f,\"t_wait\":%.3f,\"t_start\":%.3f,\"threads\":[%u,%u,%u,%u,%u,%u,%u,%lu]}",(uint64_t)now.QuadPart,double(interval)*1000/frequency.QuadPart,
                        ms[0],ms[1],ms[2],ms[3],ms[4],ms[5],ms[6],ms[7],ms[8],tid[0],tid[1],tid[2],tid[3],tid[4],tid[5],tid[6],GetCurrentThreadId());
                }
            }
        }
        {std::lock_guard lk(imageMutex);for(uint32_t i=0;i<p->swapchainCount;i++){
            auto chain=swapImages.find(p->pSwapchains[i]);VkImage image=VK_NULL_HANDLE;
            if(chain!=swapImages.end()&&p->pImageIndices[i]<chain->second.size())image=chain->second[p->pImageIndices[i]];
            auto h=outputHints.find(image);OutputHint hint{};if(h!=outputHints.end()&&h->second.present==presents&&h->second.epoch==targetEpoch.load())hint=h->second;
            event("{\"event\":\"present\",\"qpc\":%llu,\"present\":%llu,\"submission\":%llu,\"swapchain\":%llu,\"index\":%u,\"image\":%llu,\"hint_frame\":%u,\"hint_recording\":%llu,\"origin\":%llu,\"result\":%d}",(uint64_t)now.QuadPart,presents,submissions,(uint64_t)p->pSwapchains[i],p->pImageIndices[i],(uint64_t)image,hint.frame,hint.recording,(uint64_t)hint.origin,result);
        }}++presents;integrate::presented();gpuFrame.reset();
        {static LARGE_INTEGER frequency{};if(!frequency.QuadPart)QueryPerformanceFrequency(&frequency);if(survey::state.load(std::memory_order_relaxed)!=survey::kIdle)survey::presented(presents,now.QuadPart,frequency.QuadPart);}
        return result;
    }
};
VKROOTS_DEFINE_LAYER_INTERFACES(InstanceOverrides,DeviceOverrides);

BOOL WINAPI DllMain(HINSTANCE h,DWORD reason,LPVOID){
    if(reason==DLL_PROCESS_ATTACH){DisableThreadLibraryCalls(h);char path[MAX_PATH];GetModuleFileNameA(h,path,MAX_PATH);directory=path;directory=directory.substr(0,directory.find_last_of("\\/"));HANDLE t=CreateThread(nullptr,0,worker,nullptr,0,nullptr);if(t)CloseHandle(t);}return TRUE;
}
