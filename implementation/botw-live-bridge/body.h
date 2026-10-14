// Newest Minecraft state straight from the shared mapping (non-blocking); defined after live.h.
static bool liveMcNow(botwcraft::proto::McState& out);
namespace body {
static thread_local uint8_t*& base=guest;
static std::atomic<int> command{0}, generation{0};
static std::atomic<bool> active{false};
static std::atomic<uint64_t> dropped{0}, inputCalls{0}, gatedCalls{0};
static std::atomic<uint64_t> publications{0}, challenged{0};
static std::atomic<uint32_t> player{0}, publishLR{0};
static std::atomic<uint32_t> pendingAddress{0};
static std::atomic<uint64_t> sampleAttempts{0}, sampleDrops{0}, postEntries{0};
static std::atomic<uint64_t> inFlightObservation{0}, expiredObservations{0};
static std::atomic<uint64_t> badPostArgs{0}, badPostEpoch{0};
static std::atomic<uint32_t> lastPostClaim{0};
static std::atomic<uint32_t> lastPostMask{0}, lastPostMatrix{0}, lastPostPosition{0};
static std::atomic<uint64_t> observerCalls{0}, observerTicks{0}, observerMaxTicks{0};
static thread_local LONGLONG observerBegin=0;
static std::atomic_flag busy = ATOMIC_FLAG_INIT;
using PathSpec=std::array<float,6>;
static std::shared_ptr<const PathSpec> publishedPath;

static bool readable(uint32_t p, size_t n, uint32_t minimum=0x10000000) {
    if (!base || !n || n>0x8E0 || p < minimum || uint64_t(p)+n > 0xA0000000 || (p&3)) return false;
    // VirtualQuery scans to the end of a homogeneous mapping; on this Cemu lab
    // eight Post queries cost ~21 ms. Check only the bounded bytes we will read.
    // No cached mapping assumption: an inaccessible/partial range still fails.
    uint8_t scratch[0x8E0]; SIZE_T copied=0;
    return ReadProcessMemory(GetCurrentProcess(),base+p,scratch,n,&copied) && copied==n;
}
static uint32_t u32(uint32_t p) { uint32_t v; memcpy(&v, base+p, 4); return __builtin_bswap32(v); }
static float f32(uint32_t p) { uint32_t v=u32(p); float f; memcpy(&f,&v,4); return f; }
static void put32(uint32_t p,uint32_t v) { v=__builtin_bswap32(v); memcpy(base+p,&v,4); }
static void putf(uint32_t p,float f) { uint32_t v; memcpy(&v,&f,4); put32(p,v); }
static std::atomic<bool> ready{false},challenge{false};
static std::atomic<uint64_t> lifecycleEpoch{1},issuedEpoch{0},readyAt{0},sampleAt{0};
static std::atomic<uint32_t> identityActor{0},identityCtrl{0},identityBody{0};
static std::atomic<uint32_t> identityProcId{0};
static std::atomic<int> screenReason{0};
// How often a screen other than a menu's (0x2E, 0x2F the pause menu's two in BetterVR's ScreenId; 0x52, which
// is up for as long as the cross's up is held in play: the quick choice of a rune, route-221) came up: the
// title, a fade, the game over. The game changes its map only behind one of these. What was learnt of the map
// before the last one may be another map's (actors.h). Route-207: the menu counted too, and opening it took
// the walls away.
static std::atomic<uint32_t> stageScreens{0};
// Epoch of the last invalidation after which the native world may differ (a
// native screen, a replaced player). Withdrawing control alone (F8, lost
// focus, a native state the controller does not drive) leaves the world and
// the collision read from it valid; `controlOnly` records that.
static std::atomic<uint64_t> worldEpoch{1};
// Last time (GetTickCount64) the player was in a native state the controller
// does not drive (ragdoll, climbing, ...). The bridge does not claim again
// until that has been over for a moment; claiming straight away only produced
// a release on the next update, thirty times in two seconds (route-42).
static std::atomic<uint64_t> specialAt{0};
// Native states of the player (actor+0x8DC; the bit names are BetterVR's PlayerMoveBitFlags) in
// which the body write is left out. Control is not withdrawn for them: Minecraft keeps input and
// camera, and the write goes on when the state is over. Up to route-88 such a state withdrew
// control (120 times in the 196 s kept of route-65, a play session: swimming 0x410, a hit 0x80000002).
// lab/follow-states.txt: "<release> <mask, hex> <alternate>". release 1: the old behaviour.
// alternate 1: also write the controller's second body (controller flag 0x10000).
constexpr uint32_t kSpecialStates=(1u<<4)|(1u<<7)|(1u<<10)|(1u<<15)|(1u<<19)|(1u<<20)|(1u<<31);
// Written through, because the body stayed where it was written for the whole state: a hit
// (bit 31; route-90, within 0.0035 units) and swimming (bits 4 and 10; route-93, 54 s on the floor
// of a pond, standing and walking, within 0.03 units). Climbing a wall, the glider, a ragdoll and
// riding were not yet met under control and are left out.
constexpr uint32_t kSkipStates=kSpecialStates&~((1u<<31)|(1u<<4)|(1u<<10));
static std::atomic<uint32_t> skipStates{kSkipStates};
// A leash for the states that are left out (fourth number of follow-states.txt, native units).
// Link left behind is not harmless: the game keeps its world loaded around him. In route-91 he
// stayed in a pond while Minecraft went 440 units on; collision there could not be read and
// Minecraft was put back onto him five times. Further away than this, the body is written
// whatever the state.
static std::atomic<float> leash{25};
static std::atomic<uint64_t> leashWrites{0};
// Link walked by the game, part 2 (walk.h): instead of placing the body before the physics step (and stopping
// it), the body is given the speed that brings it to Steve's place in that step, so the game's physics moves it
// (its weight on what it stands on, its push on what it walks into). Placed as before when it is further from
// Steve's place than the drive's leash, in states that are not written, in a scene and while held.
// lab/walk-drive.txt: <on> <leash, native units> <push>. Default on (route-396: after the step Link was within
// 0.013 units of Steve's place in 95 of 100 updates); leash 0.5; push 1 (Steve's blocked step, below; route-401:
// the push rock moved 1.7 units, with no push it did not move).
static std::atomic<int> driveMode{1},drivePush{1};static std::atomic<float> driveLeash{.5f};
static std::atomic<uint64_t> drives{0},drivePlacements{0};
static std::atomic<bool> stateRelease{false},writeAlternate{false};
// Link faces where Steve looks (he did not at first, and the game wants him to face a shrine's
// pedestal for its A). The body's matrix, which is written with Minecraft's place, is written with Minecraft's
// yaw too. Route-208: the actor's own matrix (+0x1F8) follows the body's turn within the update (Steve 0, 90,
// 180: the actor 0, 90, 180); written alone, the actor's matrix is the body's again at the next update.
// lab/face.txt: 0 not written (as before), 1 the body's (the default), 2 the actor's, 3 both.
// Route-212, a play session: G at a shrine's pedestal with Steve looking at it (yaw -155.4, Link's the same) began the
// game's event within a tenth of a second; Link's own facing had been -27.3 since the save.
// lab/facedump.txt: a new number writes the player's first 0x4000 bytes to face-dump-N.bin. The `face` line
// (Steve's yaw, the actor's, the body's, four times a second) is in the frame log.
static std::atomic<int> faceMode{1},faceDump{0};
static std::atomic<uint64_t> suspensions{0},suspendedUpdates{0};
static void invalidate(const char* reason,bool controlOnly=false) {
    liveBodyLease=false;appliedTarget.store(nullptr);liveTarget.store(nullptr);
    command.store(0);active.store(false);challenge.store(false);
    generation.fetch_add(1,std::memory_order_release);
    uint64_t epoch=++lifecycleEpoch;if(!controlOnly)worldEpoch.store(epoch);
    LARGE_INTEGER t;QueryPerformanceCounter(&t);
    event("{\"event\":\"authority_release\",\"reason\":\"%s\",\"epoch\":%llu,\"qpc\":%llu,\"actor\":%u,\"control_only\":%d}",reason,epoch,(uint64_t)t.QuadPart,player.load(),int(controlOnly));
}
// BetterVR 2e06b31 game_state.cpp / game_structs.h. Bounded native reads,
// independent of projection and optional logger. VPAD refreshes this heartbeat.
static void lifecycle() {
    int reason=-1;
    if(readable(0x1047E650,4)) {
        uint32_t manager=u32(0x1047E650);
        if(readable(manager,0x1C)) {
            uint32_t table=u32(manager+0x18);
            if(readable(table,0x158)) {
                reason=0;
                // Background/map screen allocations stay visible in gameplay;
                // use the actual pause screen plus title/loading/fade states.
                for(int id:{1,0x2E,0x2F,0x30,0x45,0x4E,0x52,0x55}) {
                    uint32_t p=u32(table+id*4);
                    if(!p)continue;
                    if(!readable(p,0x98)){reason=-2;break;}
                    if(int8_t(base[p+0x96])!=0||int8_t(base[p+0x95])>0){reason=id;break;}
                }
            }
        }
    }
    bool nowReady=reason==0,was=ready.exchange(nowReady);
    if(screenReason.exchange(reason)!=reason&&reason!=0&&reason!=0x2E&&reason!=0x2F&&reason!=0x52)++stageScreens;
    if(nowReady)readyAt.store(GetTickCount64());
    if(was&&!nowReady)invalidate("native-screen");
    if(was!=nowReady)event("{\"event\":\"lifecycle\",\"ready\":%d,\"screen\":%d,\"epoch\":%llu}",int(nowReady),reason,lifecycleEpoch.load());
}
static bool allowed() {return ready.load()&&GetTickCount64()-readyAt.load()<500;}
static void ret(CPU* c) { c->ip=c->spr.lr;base=static_cast<uint8_t*>(getGuest()); }
static void match(CPU* c) {
    ret(c); CpuScope timing(bodyMatchCpu);frameMarks::tally(frameMarks::pMatch);
    uint32_t a=c->gpr[3], ctrl=c->gpr[4];
    c->gpr[3]=0;
    if (getTitle()!=0x00050000101C9400ULL || !readable(a,0x8E0)) return;
    uint32_t phy=u32(a+0x3A0);
    if (!readable(phy,0x54) || u32(phy+0x50)!=ctrl || !readable(ctrl,0x234)) return;
    if (memcmp(base+a+0x10,"GameROMPlayer",13)!=0) return;
    player.store(a);
    c->gpr[3]=1;
}
struct Row {
    uint64_t seq,qpc;
    uint32_t tid,actor,ctrl,body,flags,ctrlflags;
    int mode,step,write,previous,phase;
    float requested[3], observed[3], actorpos[3], ctrlpos[3], velocity[3], origin[3];
    uint64_t inputs,gated,pubs,challenges;
    uint32_t pubLR;
    uint64_t requestSeq;
};
constexpr uint32_t capacity=8192;
static Row rows[capacity];
static std::atomic<uint32_t> head{0}, tail{0};
static std::atomic<uint64_t> controlEpoch{0}, reservedObservation{0};
static Row latest;
static void getPlayer(CPU* c) {
    ret(c); CpuScope timing(bodyClaimCpu); c->gpr[3]=0;
    ++postEntries;
    if(busy.test_and_set(std::memory_order_acquire)) { ++dropped; return; }
    uint64_t epoch=controlEpoch.load(std::memory_order_acquire), seen=reservedObservation.load();
    // Claim one getter sequence before entering guest getters. Any completed
    // physics batch may claim it; host thread/core affinity is not a guarantee
    // that every selected controller update has a corresponding batch there.
    uint32_t pending=pendingAddress.load();
    uint32_t a=player.load();
    if(epoch!=seen && !inFlightObservation.load() && pending &&
        u32(pending)==uint32_t(epoch) && readable(a,0x8E0)) {
        inFlightObservation.store(epoch); put32(pending,0);
        c->gpr[3]=a; c->gpr[4]=uint32_t(epoch);
        LARGE_INTEGER t; QueryPerformanceCounter(&t); observerBegin=t.QuadPart;
    }
    busy.clear(std::memory_order_release); // never hold a host lock across guest operations
}
static void enqueue(const Row& r) {
    if(!fullEvidence.load())return;
    uint32_t h=head.load(std::memory_order_relaxed),t=tail.load(std::memory_order_acquire);
    if(h-t<capacity) { rows[h%capacity]=r; head.store(h+1,std::memory_order_release); }
    else ++dropped;
}
static void post(CPU* c) {
    ret(c); CpuScope timing(bodyPostCpu);frameMarks::mark(frameMarks::kPost);frameMarks::tally(frameMarks::pPost);
    if(observerBegin) {
        LARGE_INTEGER t; QueryPerformanceCounter(&t); uint64_t elapsed=t.QuadPart-observerBegin; observerBegin=0;
        ++observerCalls; observerTicks.fetch_add(elapsed);
        uint64_t max=observerMaxTicks.load(); while(elapsed>max && !observerMaxTicks.compare_exchange_weak(max,elapsed)) {}
    }
    uint32_t a=c->gpr[3], ctrl=c->gpr[4], b=c->gpr[5], m=c->gpr[6], pos=c->gpr[7];
    lastPostClaim.store(c->gpr[9]);
    uint32_t mask=(a!=player.load()?1:0)|(!readable(a,0x8E0)?2:0)|(!readable(ctrl,0x234)?4:0)|(!readable(b,0x74)?8:0)|(!readable(m,48)?16:0)|(!readable(pos,12)?32:0);
    lastPostMask.store(mask); lastPostMatrix.store(m); lastPostPosition.store(pos);
    // Getter outputs must be THIS wrapper's scratch frame, including workers
    // whose valid guest stacks fall below the actor/body heap address range.
    if((mask&15) || m!=c->gpr[1]+0x20 || pos!=c->gpr[1]+0x60 ||
        !readable(m,48,0x10000) || !readable(pos,12,0x10000)) { ++badPostArgs; return; }
    if(busy.test_and_set(std::memory_order_acquire)) { ++dropped; return; }
    uint32_t claimed=c->gpr[9];
    if(latest.seq!=claimed || inFlightObservation.load()!=claimed || latest.seq==reservedObservation.load()) { ++badPostEpoch; busy.clear(std::memory_order_release); return; }
    Row r=latest; r.phase=1; r.write=0;
    r.actor=a; r.ctrl=ctrl; r.body=b; // actual post-getter identity, not a cached identity
    LARGE_INTEGER qpc; QueryPerformanceCounter(&qpc); r.qpc=qpc.QuadPart;
    r.tid=GetCurrentThreadId(); r.pubs=++publications; r.pubLR=c->gpr[8]; publishLR.store(r.pubLR);
    r.flags=u32(a+0x8DC); r.ctrlflags=u32(ctrl+0xE8);
    uint32_t motion=u32(b+0x70);
    bool motionReadable=readable(motion,0x1C0);
    for(int i=0;i<3;++i) {
        r.observed[i]=f32(m+12+i*16); r.actorpos[i]=f32(a+0x204+i*16); r.ctrlpos[i]=f32(pos+i*4);
        r.velocity[i]=motionReadable?f32(motion+0x1B0+i*4):NAN;
    }
    enqueue(r); reservedObservation.store(r.seq); inFlightObservation.store(0); busy.clear(std::memory_order_release);
}
static double finiteOr0(double v){return std::isfinite(v)?v:0;} // JSON events cannot carry NaN
static void sample(CPU* c) {
    ret(c); CpuScope timing(bodySampleCpu);frameMarks::mark(frameMarks::kSample);
    uint32_t a=c->gpr[3], ctrl=c->gpr[4], b=c->gpr[5], m=c->gpr[6], pos=c->gpr[7];
    uint32_t pending=c->gpr[8];
    uint64_t seq=++sampleAttempts; // count every selected callback, including failed samples
    c->gpr[3]=0;
    c->gpr[8]=0;
    // stack scratch lives below 0x10000000 on some builds; VirtualQuery it separately.
    if (!base || !readable(a,0x8E0) || !readable(ctrl,0x234) || !readable(b,0x74) ||
        m<0x10000000 || !readable(m,48) || !readable(pos,12) ||
        pending<0x01800000 || pending>=0x01C00000 || !readable(pending,4,0x01800000)) {if(command.load())invalidate("invalid-player");return;}
    if (busy.test_and_set(std::memory_order_acquire)) { ++dropped; ++sampleDrops; return; }
    // An incomplete observation expires when the next controller sample arrives.
    // Its late Post carries its own epoch and cannot publish against this sample.
    if(inFlightObservation.exchange(0)) ++expiredObservations;
    pendingAddress.store(pending);
    sampleAt.store(GetTickCount64());
    // The controller's second body (controller flag 0x10000) taking over is not another player.
    if(identityActor.load()&&(identityActor.load()!=a||identityCtrl.load()!=ctrl||identityProcId.load()!=u32(a+0x50)))invalidate("player-replaced");
    identityActor.store(a);identityCtrl.store(ctrl);identityBody.store(b);identityProcId.store(u32(a+0x50));
    if(!allowed()&&command.load())invalidate("readiness-expired");
    static int seen=-1, mode=0, step=0;
    static float start[3]{}, prior[3]{};
    static bool previous=false;static uint64_t priorSeq=0;
    static bool holding=false;static uint64_t heldSince=0;bool held=false;
    if(sceneOn.load())sceneHold=true;
    bool scene=sceneHold.load(),targetOk=true;   // a scene of the game's: Link is the game's, control is kept (live_target.h)
    static std::shared_ptr<const PathSpec> pathSpec;
    static bool compatible=false;
    Row r{};
    r.seq=seq; LARGE_INTEGER now; QueryPerformanceCounter(&now); r.qpc=now.QuadPart;
    r.tid=GetCurrentThreadId(); r.actor=a; r.ctrl=ctrl; r.body=b;
    r.flags=u32(a+0x8DC); r.ctrlflags=u32(ctrl+0xE8);
    for(int i=0;i<3;++i) {
        r.observed[i]=f32(m+12+i*16); r.actorpos[i]=f32(a+0x204+i*16);
        r.ctrlpos[i]=f32(pos+i*4); r.velocity[i]=NAN;
        r.requested[i]=previous?prior[i]:NAN;
    }
    r.previous=previous;
    r.requestSeq=previous?priorSeq:0;
    uint32_t motion=u32(b+0x70);
    if(readable(motion,0x1C0)) for(int i=0;i<3;++i) r.velocity[i]=f32(motion+0x1B0+i*4);
    int gen=generation.load(std::memory_order_acquire);
    if(gen!=seen) {
        seen=gen; mode=issuedEpoch.load()==lifecycleEpoch.load()?command.load():0; step=0; previous=false;
        tickClock::reset();
        for(int i=0;i<3;++i) start[i]=r.observed[i];
        pathSpec=std::atomic_load(&publishedPath);
        float distance2=0;
        if(pathSpec) for(int i=0;i<3;++i) distance2+=std::pow((*pathSpec)[i]-start[i],2);
        compatible=pathSpec && distance2<=.01f; // refuse stale start >0.1 native unit
    }
    bool finite=true;
    for(float x:r.observed) finite &= std::isfinite(x);
    // The write is made for the ordinary main-body controller. In another native state it is
    // left out; only the fixed test modes (and release 1) still give control up for it.
    bool blockedState=(r.flags&skipStates.load())||((r.ctrlflags&0x10000)&&!writeAlternate.load());
    bool releaseForState=blockedState&&(stateRelease.load()||mode!=5);
    if(releaseForState)specialAt.store(GetTickCount64());
    bool drive=mode && allowed() && issuedEpoch.load()==lifecycleEpoch.load() && finite && !releaseForState;
    int duration=mode==5?INT_MAX:(mode==1 || mode==2 || mode==4)?480:mode==3?1:0;
    drive &= step<duration;
    if(mode==4) drive &= compatible;
    AppliedTarget applied;auto targetSnapshot=liveTarget.load();
    if(mode==5){
        // The publication can occur after the earlier getter timestamp. Take
        // this request's clock AFTER acquiring its immutable publication.
        QueryPerformanceCounter(&now);r.qpc=now.QuadPart;
        LARGE_INTEGER frequency;QueryPerformanceFrequency(&frequency);
        bool targetValid=liveBodyLease.load()&&liveGameplayLease()&&targetSnapshot&&
            targetSnapshot->bodyEpoch==lifecycleEpoch.load()&&targetSnapshot->collisionEpoch==liveCollisionEpoch.load()&&
            targetSnapshot->until>=GetTickCount64();
        if(targetValid){
            if(targetSnapshot->unbounded){
                // Native stream: tick history, fed with the freshest state available.
                botwcraft::proto::McState latest=targetSnapshot->source;liveMcNow(latest);
                targetValid=interpolateHistory(*targetSnapshot,latest,r.qpc,frequency.QuadPart,applied);
            }else targetValid=interpolateTarget(*targetSnapshot,r.qpc,frequency.QuadPart,applied);
        }
        // Which gate refused: 1 lease, 2 gameplay lease, 4 no target, 8 body
        // epoch, 16 collision epoch, 32 expired, 64 interpolation (see reject).
        int why=(!liveBodyLease.load())|(!liveGameplayLease())<<1|(!targetSnapshot)<<2;
        if(targetSnapshot)why|=(targetSnapshot->bodyEpoch!=lifecycleEpoch.load())<<3|(targetSnapshot->collisionEpoch!=liveCollisionEpoch.load())<<4|(targetSnapshot->until<GetTickCount64())<<5;
        if(!targetValid&&!why)why=64;
        if(!drive||!targetValid)event("{\"event\":\"live_target_reject\",\"qpc\":%llu,\"body_valid\":%d,\"flags\":%u,\"controller_flags\":%u,\"target_valid\":%d,\"why\":%d,\"gameplay\":%d,\"interpolate\":%d,\"source_seq\":%u,\"tick_qpc\":%lld,\"received_qpc\":%llu}",r.qpc,int(drive),r.flags,r.ctrlflags,int(targetValid),why,liveGameplayReason(),applied.reject,targetSnapshot?targetSnapshot->source.seq:0,targetSnapshot?(long long)targetSnapshot->source.tickQpc:0,targetSnapshot?targetSnapshot->receivedQpc:0);
        // A short freeze of Minecraft: nothing refuses but its silence (why: the gameplay lease, the target's age, the
        // history). The body stays where it was last written and control is kept, so that the bridge holds.
        // After staleHoldMs it is given up as before.
        // The tick history refuses first (its codes 3 and 4: the tick or its arrival older than 250 ms; route-296), then the lease.
        held=!scene&&drive&&!targetValid&&!(why&~(2|32|64))&&(why!=64||applied.reject==3||applied.reject==4)&&(holding?GetTickCount64()-heldSince<staleHoldMs.load():previous)&&liveGameplayHeld();
        targetOk=targetValid;
        // In a scene only what ends the claim itself gives control up (the lease, the epochs), not Minecraft's target.
        if(scene)drive &= targetValid||!(why&(1|8|16));
        else if(!held)drive &= targetValid;
    }
    if(held!=holding){
        if(held)heldSince=GetTickCount64();
        event("{\"event\":\"body_hold\",\"state\":\"%s\",\"ms\":%llu,\"kept\":%d,\"body\":[%.9g,%.9g,%.9g]}",held?"held":"over",held?0ULL:GetTickCount64()-heldSince,int(held||drive),prior[0],prior[1],prior[2]);
        holding=held;
    }
    if(mode && step<duration && !drive) {invalidate("unsupported-body-or-target",allowed()&&finite); mode=0;}
    // Minecraft keeps control (`drive`); `write` is whether this update moves the body.
    static bool suspended=false;static uint32_t suspendedFlags=0;static uint64_t suspendedSince=0,pulls=0;
    bool suspend=drive&&(blockedState||(scene&&mode==5)),pulled=false;
    if(suspend&&mode==5&&!held&&!scene){float d2=0,limit=leash.load();for(int i=0;i<3;i++)d2+=std::pow(applied.body[i]-r.observed[i],2);pulled=d2>limit*limit;}
    bool write=drive&&(!suspend||pulled);
    if(suspend)++suspendedUpdates;
    if(pulled){++leashWrites;++pulls;}
    if(suspend!=suspended||(suspend&&r.flags!=suspendedFlags)){
        if(suspend&&!suspended){++suspensions;suspendedSince=GetTickCount64();pulls=pulled;}
        event("{\"event\":\"body_follow\",\"qpc\":%llu,\"state\":\"%s\",\"flags\":%u,\"controller_flags\":%u,\"ms\":%llu,\"leash_writes\":%llu,\"body\":[%.9g,%.9g,%.9g]}",r.qpc,suspend?"suspended":"resumed",r.flags,r.ctrlflags,suspended?GetTickCount64()-suspendedSince:0ULL,pulls,r.observed[0],r.observed[1],r.observed[2]);
        suspended=suspend;suspendedFlags=r.flags;
    }
    r.mode=mode; r.step=step; r.write=write;
    for(int i=0;i<3;++i) r.origin[i]=start[i];
    if(drive) {
        float target[3]={start[0],start[1],start[2]};
        if(mode==2) {
            int leg=step/120; float t=float(step%120+1)/120;
            if(leg==0) target[0]+=2*t;
            if(leg==1) target[0]+=2*(1-t);
            if(leg==2) target[2]+=2*t;
            if(leg==3) target[2]+=2*(1-t);
        }
        if(mode==3) target[1]+=2; // single elevated placement, then native fall
        if(mode==4) {
            float t=step<240?float(step+1)/240:float(479-step)/240;
            for(int i=0;i<3;++i) target[i]=(*pathSpec)[i]+t*((*pathSpec)[i+3]-(*pathSpec)[i]);
        }
        if(mode==5&&held)std::copy_n(prior,3,target);   // the place last written, again
        else if(mode==5&&targetOk){
            // Published whether or not the body is written: the camera follows Minecraft's player.
            std::copy_n(applied.body,3,target);applied.sequence=seq;
            appliedTarget.store(std::make_shared<const AppliedTarget>(applied));
            const auto& s=applied.request.source;
            if(write)event("{\"event\":\"live_body_write\",\"seq\":%llu,\"qpc\":%llu,\"source_seq\":%u,\"session\":%llu,\"body_epoch\":%llu,\"collision_epoch\":%llu,\"tick_qpc\":%lld,\"received_qpc\":%llu,\"tick_ms\":%.9g,\"alpha\":%.9g,\"flags\":%u,\"ack\":%u,\"prev\":[%.12g,%.12g,%.12g],\"cur\":[%.12g,%.12g,%.12g],\"target\":[%.9g,%.9g,%.9g],\"feet_y\":%.9g,\"support\":%d,\"footprint\":%.12g,\"centre\":%.12g,\"predicted\":%.12g,\"gap\":%.12g,\"native_flags\":%u,\"eye_height\":%.9g,\"delay_ms\":%.4g,\"ticks\":%.4g}",seq,r.qpc,s.seq,applied.request.session,applied.request.bodyEpoch,applied.request.collisionEpoch,(long long)s.tickQpc,applied.request.receivedQpc,s.tickMs,applied.alpha,s.flags,s.teleportAck,s.prevX,s.prevY,s.prevZ,s.curX,s.curY,s.curZ,target[0],target[1],target[2],applied.feet[1],applied.support.kind,finiteOr0(applied.support.footprint),finiteOr0(applied.support.centre),finiteOr0(applied.support.predicted),finiteOr0(applied.support.gap),r.flags,applied.eyeHeight,applied.delayMs,applied.ticks);
        }
        if(write){
            for(int i=0;i<3;++i) { putf(m+12+i*16,target[i]); prior[i]=target[i]; }
            priorSeq=seq;
        }
    }
    {
        botwcraft::proto::McState mc{};bool known=liveMcNow(mc);int fm=faceMode.load();
        auto yawOf=[&](uint32_t mtx){return double(std::atan2(-f32(mtx+8),f32(mtx+0x28)))*57.29578;};
        double actorYaw=yawOf(a+0x1F8),bodyYaw=yawOf(m);
        if(write&&known&&fm){
            // Minecraft's yaw: 0 looks along +z, 90 along -x. The matrix's third column is the way faced.
            float y=mc.yaw*.01745329252f,cs=std::cos(y),sn=std::sin(y);
            auto turn=[&](uint32_t mtx){putf(mtx,cs);putf(mtx+4,0);putf(mtx+8,-sn);putf(mtx+0x10,0);putf(mtx+0x14,1);putf(mtx+0x18,0);putf(mtx+0x20,sn);putf(mtx+0x24,0);putf(mtx+0x28,cs);};
            if(fm&1)turn(m);
            if(fm&2)turn(a+0x1F8);
        }
        static uint64_t told=0;uint64_t t=GetTickCount64();
        if(t-told>=250){told=t;event("{\"event\":\"face\",\"mode\":%d,\"write\":%d,\"steve\":%.5g,\"actor\":%.5g,\"body\":%.5g,\"flags\":%u}",fm,int(write),known?finiteOr0(mc.yaw):0.0,finiteOr0(actorYaw),finiteOr0(bodyYaw),r.flags);}
        static int dumped=0;int token=faceDump.load();
        if(token!=dumped){
            dumped=token;
            if(FILE* f=fopen((directory+"/face-dump-"+std::to_string(token)+".bin").c_str(),"wb")){
                for(uint32_t at=0;at<0x4000;at+=0x800){uint8_t zero[0x800]{};fwrite(readable(a+at,0x800)?base+a+at:zero,1,0x800,f);}
                fclose(f);event("{\"event\":\"face\",\"dump\":%d,\"actor_at\":%u,\"actor\":%.5g}",token,a,finiteOr0(actorYaw));
            }
        }
    }
    previous=write;
    active.store(drive && mode!=3);
    ++step;
    if(mode && step>=duration) challenge.store(false);
    r.inputs=inputCalls.load(); r.gated=gatedCalls.load();
    r.pubs=publications.load(); r.pubLR=publishLR.load(); r.challenges=challenged.load();
    enqueue(r);
    latest=r; latest.previous=write;
    if(write){for(int i=0;i<3;++i) latest.requested[i]=prior[i];latest.requestSeq=seq;}
    controlEpoch.store(r.seq,std::memory_order_release);
    int answer=write?1:0;
    {   // Part 2 (above): the speed instead of the place.
        static LONGLONG lastQpc=0;static float worst=0,pushing=0;static uint64_t told=0;
        LARGE_INTEGER q,f;QueryPerformanceCounter(&q);QueryPerformanceFrequency(&f);
        double dt=lastQpc?double(q.QuadPart-lastQpc)/double(f.QuadPart):0;lastQpc=q.QuadPart;
        if(write&&driveMode.load()&&mode==5&&!held&&!scene&&!suspend&&targetOk&&!(r.flags&(1u<<10))&&dt>1/240.&&dt<.1){   // not swimming (not tried)
            float gap[3],d2=0;for(int i=0;i<3;i++){gap[i]=prior[i]-r.observed[i];d2+=gap[i]*gap[i];}
            float leashNow=driveLeash.load();worst=std::max(worst,std::sqrt(d2));
            if(d2<=leashNow*leashNow&&readable(pending+4,16,0x01800000)){
                float speed[3];for(int i=0;i<3;i++)speed[i]=float(gap[i]/dt);
                // What Minecraft's collision took from Steve's step (McState pushX/Z, version 22): Link keeps
                // pushing at that speed, so the game's physics moves what stopped them (a round boulder); the gap
                // above pulls him back to Steve's place in the next step.
                botwcraft::proto::McState now{};
                if(drivePush.load()&&liveMcNow(now)){
                    float px=now.pushX*nativeRegions::scale,pz=now.pushZ*nativeRegions::scale,size=std::sqrt(px*px+pz*pz);
                    if(std::isfinite(size)&&size>.05f){
                        // Minecraft's box is square and turned with the world, Link's body round: pushing at a slant,
                        // the box's corner stops Steve up to (sqrt 2 - 1) half-widths before Link would touch, so a
                        // push at a slant was at times not felt by the game. The push reaches that gap plus a little in this step. Route-408, at 45 degrees
                        // towards open ground: the rock moved 1.7 units along the push. Route-407, at 45 degrees
                        // towards the cliff behind it: Link pressed on it (7 units a second) and it stayed, wedged.
                        float half=.3f*nativeRegions::scale,corner=half*((std::fabs(px)+std::fabs(pz))/size-1.f);
                        float want=std::max(size,(corner+.08f)/float(dt)),k=std::min(9.f,want)/size;
                        speed[0]+=px*k;speed[2]+=pz*k;pushing=size*k;}
                }
                for(int i=0;i<3;i++)putf(pending+4+i*4,speed[i]);putf(pending+16,0);
                answer=2;++drives;
            }else ++drivePlacements;
        }
        uint64_t t=GetTickCount64();
        if(driveMode.load()&&t-told>=500){told=t;event("{\"event\":\"walk_drive\",\"drives\":%llu,\"placements\":%llu,\"worst\":%.4f,\"push\":%.3f,\"answer\":%d,\"dt\":%.4f,\"flags\":%u}",drives.load(),drivePlacements.load(),worst,pushing,answer,dt,r.flags);worst=0;pushing=0;}
    }
    c->gpr[3]=answer;
    c->gpr[8]=uint32_t(seq);
    busy.clear(std::memory_order_release);
}

static FILE* csv;
static constexpr const char* csvHeader="seq,qpc,thread,actor,controller,body,flags,controller_flags,mode,step,write,previous_request,requested_x,requested_y,requested_z,body_x,body_y,body_z,actor_x,actor_y,actor_z,controller_x,controller_y,controller_z,velocity_x,velocity_y,velocity_z,origin_x,origin_y,origin_z,input_calls,gated_calls,native_publications,publish_lr,gated_challenges,phase,request_seq\n";
static void initialize(Register reg) {
    reg("coreinit","Bridge_Match",match);reg("coreinit","Bridge_Sample",sample);
    reg("coreinit","Bridge_Post",post);reg("coreinit","Bridge_GetPlayer",getPlayer);
    csv=fopen((directory+"/poses.csv").c_str(),"w");
    if(csv)fputs(csvHeader,csv);
}
static void poll(FILE* info) {
    if(command.load()&&(!allowed()||GetTickCount64()-sampleAt.load()>500))invalidate("update-heartbeat-expired");
    {static uint64_t checked=0;auto now=GetTickCount64();if(now-checked>=500){
        checked=now;int release=0,alternate=0;uint32_t mask=kSkipStates;float limit=25;std::ifstream file(directory+"/follow-states.txt");
        if(file>>release){file>>std::hex>>mask>>std::dec>>alternate;if(!(file>>limit)||!(limit>0))limit=25;}
        stateRelease=release==1;skipStates=mask;writeAlternate=alternate==1;leash=limit;
        {int on=1,push=1;float most=.5f;std::ifstream drive(directory+"/walk-drive.txt");drive>>on>>most>>push;driveMode=on;driveLeash=most>0&&most<25?most:.5f;drivePush=push;}
        {int face=1,dump=0;std::ifstream(directory+"/face.txt")>>face;std::ifstream(directory+"/facedump.txt")>>dump;faceMode=face;faceDump=dump;}
    }}
    static std::string last;
    std::ifstream file(directory+"/command.txt");std::string line;std::getline(file,line);
    if(!line.empty()&&line!=last) {
        last=line;
        int mode=line.starts_with("external")?4:line.starts_with("path")?2:line.starts_with("hold")?1:line.starts_with("fall")?3:0;
        if(mode==4) {
            auto spec=std::make_shared<PathSpec>();std::ifstream file(directory+"/path_spec.txt");bool valid=true;
            for(float& v:*spec)valid &= bool(file>>v)&&std::isfinite(v)&&std::abs(v)<100000;
            std::string extra;valid &= !(file>>extra);
            float distance2=0;for(int i=0;i<3;++i)distance2+=std::pow((*spec)[i+3]-(*spec)[i],2);
            valid &= distance2<=.75f*.75f&&std::abs((*spec)[4]-(*spec)[1])<=.35f;
            if(valid)std::atomic_store(&publishedPath,std::shared_ptr<const PathSpec>(spec));else mode=0;
        }
        if(mode&&!allowed()) {mode=0;fprintf(info,"body command rejected: native state unavailable\n");}
        issuedEpoch.store(lifecycleEpoch.load());command.store(mode);
        challenge.store(mode==1&&line.find("forward")!=std::string::npos);
        if(!mode)active.store(false);
        generation.fetch_add(1,std::memory_order_release);
        fprintf(info,"body command %s epoch=%llu mode=%d\n",line.c_str(),issuedEpoch.load(),mode);fflush(info);
    }
    uint32_t t=tail.load(),h=head.load(std::memory_order_acquire);
    while(t!=h) {
        const Row& r=rows[t%capacity];
        if(trace::room(csv,directory+"/poses.csv",csvHeader,trace::poseRotations)) {
            fprintf(csv,"%llu,%llu,%u,%u,%u,%u,%u,%u,%d,%d,%d,%d",r.seq,r.qpc,r.tid,r.actor,r.ctrl,r.body,r.flags,r.ctrlflags,r.mode,r.step,r.write,r.previous);
            for(auto p:{r.requested,r.observed,r.actorpos,r.ctrlpos,r.velocity,r.origin})for(int i=0;i<3;++i)fprintf(csv,",%.9g",p[i]);
            fprintf(csv,",%llu,%llu,%llu,%u,%llu,%d,%llu\n",r.inputs,r.gated,r.pubs,r.pubLR,r.challenges,r.phase,r.requestSeq);
        }++t;
    }
    tail.store(t,std::memory_order_release);if(csv)fflush(csv);
}
} // namespace body
