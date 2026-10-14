// Existing worker schedules native requests; callbacks only consume owned jobs.
#pragma once
namespace nativeStream {
static bool clearPending=false;
// Session: MC is first teleported onto Link (`teleport`), then moves only
// while its body, head and travel volume is covered by complete, fresh,
// consumed native data. Losing coverage parks MC (kBotwLoading) and keeps the
// published collision; regaining it resumes in place.
static bool staged=false,ready=false,paused=false;
static uint32_t teleport=0;static double stagedLink[3]{}; // Link's place (MC coordinates) when the session was staged
static double spawn[3]{},link[3]{};
static uint64_t pauses=0,resumes=0,clock=0; // clock: worker time of the last progress()
// A coverage pause is first a wait (kBotwHold; lab/coverage-hold.txt 0: never): Minecraft stays where it
// is with its keys held and continues at `heldVelocity`. SkyCraft's loading park, the only pause
// before, releases the keys, drops the velocity and lets go only with ground within 12 blocks
// below or after 6 s (BotwClient.holdUntilReady): right for a loading screen, seconds long in flight.
// `parkPauses`/`parkResumes` count the pauses that did become a park (the keys were released).
static bool holding=false;static std::atomic<bool> lightHold{true};
static double heldVelocity[3]{};static uint64_t pausedAt=0,parkedAt=0,parkPauses=0,parkResumes=0;
// kSettleMs, kStageDrift: see the ready test in progress().
constexpr uint64_t kSettleMs=300;constexpr double kStageDrift=2;
constexpr uint64_t kHoldMs=1500,kParkMs=150; // kParkMs: a wait that became a park lasts long enough for a Minecraft tick to see it
constexpr double kMaxSpeed=100; // MC/s per axis taken as a velocity (a rocket-driven elytra: about 34)
static std::shared_ptr<const NativeSupport> supportSnapshot;
static uint64_t readySerial=0,softReleases=0;static bool restage=false;
// Control withdrawn, world unchanged: the cache and the collision Minecraft
// holds stay; only the movement lease ends and MC is staged again.
static void release(uint64_t bodyEpoch){
    staged=ready=paused=false;walking::ready=false;supportSnapshot.reset();restage=true;++softReleases;
    nativeRegions::cache.rebase(bodyEpoch);nativeRegions::requested.store(nullptr);
    liveBodyLease=false;liveTarget.store(nullptr);appliedTarget.store(nullptr);physicalBlocked=true;
    event("{\"event\":\"native_region_release\",\"body_epoch\":%llu,\"generation\":%llu,\"entries\":%zu,\"collision_epoch\":%u}",bodyEpoch,nativeRegions::cache.stamp.generation,nativeRegions::cache.entries.size(),walking::epoch);
}
static void reset(uint64_t bodyEpoch,uint64_t peer,uint32_t world,const char* reason){
    restage=false;
    staged=ready=paused=false;walking::ready=false;supportSnapshot.reset();
    // Only genuine lifecycle changes clear Minecraft collision: stream switch,
    // body/load epoch, peer session or native world identity.
    ++trace::cacheWipes;
    nativeRegions::cache.reset(bodyEpoch,peer,world);nativeRegions::requested.store(nullptr);nativeShapes::decodedRoots.clear();nativeMovers::clear();
    liveCollisionEpoch=++walking::epoch;clearPending=true;
    liveBodyLease=false;liveTarget.store(nullptr);appliedTarget.store(nullptr);physicalBlocked=true;
    event("{\"event\":\"native_region_reset\",\"reason\":\"%s\",\"body_epoch\":%llu,\"peer\":%llu,\"world\":%u,\"generation\":%llu,\"collision_epoch\":%u}",reason,bodyEpoch,peer,world,nativeRegions::cache.stamp.generation,walking::epoch);
}
template<class T> static walking::Packet packet(const nativeRegions::Key& key,const std::vector<T>& values,uint32_t type){
    botwcraft::proto::ColRegion h{};
    h.minX=key[0]*8;h.minY=key[1]*8;h.minZ=key[2]*8;
    h.maxX=h.minX+7;h.maxY=h.minY+7;h.maxZ=h.minZ+7;h.count=uint32_t(values.size());
    walking::Packet result{type,std::vector<uint8_t>(sizeof(h)+values.size()*sizeof(T))};
    memcpy(result.bytes.data(),&h,sizeof(h));if(!values.empty())memcpy(result.bytes.data()+sizeof(h),values.data(),values.size()*sizeof(T));
    return result;
}
static void publish(uint64_t now){
    if(!live::connected||live::failed||!live::base)return;
    if(clearPending){
        if(!walking::push({botwcraft::proto::kColClear,std::vector<uint8_t>(4)}))return;
        clearPending=false;
    }
    // Complete data is sent once per change; four packets (two regions) per
    // poll bound the worker's time. Failed refreshes and age never trigger a clear.
    unsigned budget=4;
    for(auto& [key,entry]:nativeRegions::cache.entries){
        if(!entry.complete||entry.publishPhase>=2)continue;
        if(!entry.publishPhase){
            if(!walking::push(packet(key,entry.triangles,botwcraft::proto::kColTris)))return;
            entry.publishPhase=1;if(!--budget)return;
        }
        if(!walking::push(packet(key,entry.blocks,botwcraft::proto::kColRegion)))return;
        entry.publishPhase=2;entry.publishedHead=walking::publishedHead;
        if(!entry.stableHead)entry.stableHead=entry.publishedHead;
        if(!--budget)return;
    }
}
// First exported surface met by a downward segment, in MC space; NaN if none.
static double exportedGround(double x,double z,double top,double bottom,float* normal){
    double best=NAN;
    for(const auto& [key,entry]:nativeRegions::cache.entries){
        if(!entry.complete||x<key[0]*8-.5||x>key[0]*8+8.5||z<key[2]*8-.5||z>key[2]*8+8.5)continue;
        for(const auto& t:entry.triangles){
            const float *a=t.v,*b=t.v+3,*c=t.v+6;
            double d=(b[2]-c[2])*(a[0]-c[0])+(c[0]-b[0])*(a[2]-c[2]);
            if(std::abs(d)<1e-9)continue;
            double u=((b[2]-c[2])*(x-c[0])+(c[0]-b[0])*(z-c[2]))/d,v=((c[2]-a[2])*(x-c[0])+(a[0]-c[0])*(z-c[2]))/d;
            if(std::min({u,v,1-u-v})<-1e-6)continue;
            double y=u*a[1]+v*b[1]+(1-u-v)*c[1];
            if(y>top+1e-6||y<bottom-1e-6||(std::isfinite(best)&&y<=best))continue;
            best=y;float e1[3],e2[3];nativeGeometry::sub(b,a,e1);nativeGeometry::sub(c,a,e2);nativeGeometry::cross(e1,e2,normal);
            float n=std::sqrt(nativeGeometry::dot(normal,normal));for(int k=0;k<3;k++)normal[k]/=n;
        }
    }
    return best;
}
// Measured native ground versus the exported mesh at the stream ray's sample.
static uint64_t groundChecks=0,groundMisses=0;
static void groundCheck(){
    static uint64_t seen=0;auto h=calibration::lastHit.load();
    if(!h||h->id==seen||!(h->id>>63)||h->epoch!=body::lifecycleEpoch.load())return;
    seen=h->id;if(!h->hit)return;
    auto mc=[](float v,int k){return (double(v)-nativeRegions::anchor[k])/nativeRegions::scale+nativeRegions::origin[k];};
    double x=mc(h->point[0],0),z=mc(h->point[2],2),native=mc(h->point[1],1);
    float normal[3]{};double exported=exportedGround(x,z,mc(h->from[1],1),mc(h->from[1]+h->delta[1],1),normal);
    ++groundChecks;if(!std::isfinite(exported))++groundMisses;
    event("{\"event\":\"ground_check\",\"id\":%llu,\"native_point\":[%.9g,%.9g,%.9g],\"mc\":[%.9g,%.9g,%.9g],\"exported_y\":%.9g,\"diff_native\":%.9g,\"normal\":[%.6g,%.6g,%.6g],\"checks\":%llu,\"misses\":%llu}",
        h->id,h->point[0],h->point[1],h->point[2],x,native,z,std::isfinite(exported)?exported:0.,std::isfinite(exported)?(exported-native)*nativeRegions::scale:0.,normal[0],normal[1],normal[2],groundChecks,groundMisses);
}
static uint64_t consumedTail(){
    if(!live::base)return 0;
    return live::atom(*reinterpret_cast<uint64_t*>(live::base+botwcraft::proto::kOffCollisionRing+0x40)).load(std::memory_order_acquire);
}
// Body (feet-1 for the ground below, +1.8 head) and travel volume, using the
// same 0.6 s lookahead that orders acquisition.
static bool covered(const double* p,const double* v,uint64_t now,nativeRegions::Key* failed=nullptr,int* reason=nullptr){
    double lo[3],hi[3];nativeRegions::travelBox(p,v,lo,hi);
    return nativeRegions::cache.covers(lo,hi,now,consumedTail(),failed,reason);
}
static uint64_t qpcNow(){LARGE_INTEGER q;QueryPerformanceCounter(&q);return uint64_t(q.QuadPart);}
// Bodies that move (native_movers.h), before the regions: each one whole in a message of its own whenever it has
// moved or changed, with its transform (Minecraft's space); one that is no more is taken back. A body that rests is
// sent once.
static void publishMovers(uint64_t now){
    using namespace botwcraft::proto;namespace nm=nativeMovers;
    auto send=[&](uint32_t id,uint32_t flags,uint32_t serial,const float* xf,const std::vector<ColTri>& tris){
        ColBody h{};h.id=id;h.flags=flags;h.serial=serial;h.count=uint32_t(tris.size());
        if(xf)for(int k=0;k<3;k++){
            for(int j=0;j<3;j++)h.transform[j*3+k]=xf[j*4+k];
            h.transform[9+k]=(xf[12+k]-nativeRegions::anchor[k])/nativeRegions::scale+nativeRegions::origin[k];
        }
        walking::Packet packet{kColBody,std::vector<uint8_t>(sizeof(h)+tris.size()*sizeof(ColTri))};
        memcpy(packet.bytes.data(),&h,sizeof(h));if(!tris.empty())memcpy(packet.bytes.data()+sizeof(h),tris.data(),tris.size()*sizeof(ColTri));
        return walking::push(packet);
    };
    while(!nm::gone.empty()){
        if(!send(nm::gone.back(),kBodyGone,0,nullptr,{}))return;
        if(nm::mode.load()==2)event("{\"event\":\"native_mover\",\"body\":%u,\"gone\":1}",nm::gone.back());
        nm::gone.pop_back();++nm::goneSent;++nm::revision;
    }
    if(nm::mode.load()){
        double p[]{live::mc.curX,live::mc.curY,live::mc.curZ};
        for(auto& [body,m]:nm::movers){
            if(m.parts.empty())continue;
            bool moved=!m.told||m.dirty;
            for(int i=0;i<15&&!moved;i++)if(i%4!=3)moved=std::abs(m.xf[i]-m.sentXf[i])>(i<12?1e-5f:1e-4f);
            if(!moved)continue;
            double away=0,at[3];   // `far` is a word of windows.h
            for(int k=0;k<3;k++){at[k]=(double(m.xf[12+k])-nativeRegions::anchor[k])/nativeRegions::scale+nativeRegions::origin[k];away=std::max(away,std::abs(at[k]-p[k]));}
            if(!(away<=nm::kRange+m.reach/nativeRegions::scale))continue;   // also when Minecraft's place is not a number
            std::vector<ColTri> tris;nm::minecraft(m,tris);if(tris.empty())continue;
            if(!send(body,0,++m.serial,m.xf,tris))return;
            if(nm::mode.load()==2)event("{\"event\":\"native_mover\",\"body\":%u,\"serial\":%u,\"triangles\":%zu,\"at\":[%.4f,%.4f,%.4f],\"game\":[%.4f,%.4f,%.4f],\"qpc\":%llu}",body,m.serial,tris.size(),at[0],at[1],at[2],m.xf[12],m.xf[13],m.xf[14],qpcNow());
            m.told=true;m.dirty=false;std::copy_n(m.xf,16,m.sentXf);m.sent=std::move(tris);++nm::published;++nm::revision;
        }
    }
    static uint64_t told=0;
    if(now-told>=1000){
        told=now;size_t shaped=0,sent=0,triangles=0;for(const auto& [body,m]:nm::movers){shaped+=!m.parts.empty();sent+=m.told;triangles+=m.sent.size();}
        event("{\"event\":\"native_movers\",\"mode\":%d,\"bodies\":%zu,\"with_parts\":%zu,\"in_minecraft\":%zu,\"triangles\":%zu,\"messages\":%llu,\"taken_back\":%llu,\"parts_changed\":%llu}",nm::mode.load(),nm::movers.size(),shaped,sent,triangles,nm::published,nm::goneSent,nm::geometry);
    }
}
static void mcMotion(double* position,double* velocity){
    position[0]=live::mc.curX;position[1]=live::mc.curY;position[2]=live::mc.curZ;
    double previous[]{live::mc.prevX,live::mc.prevY,live::mc.prevZ};
    for(int k=0;k<3;k++)velocity[k]=live::mc.tickMs>0&&std::isfinite(previous[k])&&std::isfinite(position[k])?std::clamp((position[k]-previous[k])*1000/live::mc.tickMs,-kMaxSpeed,kMaxSpeed):0;
}
static void configure(botwcraft::proto::BotwState& sky){
    using namespace botwcraft::proto;
    sky.collisionEpoch=walking::epoch;
    // Like SkyCraft, MC is told where the native player stands; it is used
    // only when MC teleports (stage, park/resume, new player object).
    sky.teleportSeq=teleport;sky.posX=link[0];sky.posY=link[1];sky.posZ=link[2];
    if(!(sky.flags&kBotwInGame))return; // Native menu/loading already parks MC.
    double still[3]{};bool hold=!staged||(ready?paused:!covered(spawn,still,clock));
    // Staging moves Minecraft, like a loading screen. A coverage pause only makes it wait (kBotwHold).
    if(hold)sky.flags|=staged&&ready&&holding?kBotwHold:kBotwLoading;
}
static void progress(uint64_t now){
    using namespace botwcraft::proto;
    clock=now;
    if(!live::connected||live::failed||!live::base)return;
    if(!staged){
        if(!body::allowed()||now-body::sampleAt.load()>250||body::busy.test_and_set())return;
        auto pose=body::latest;body::busy.clear();if(!pose.actor)return;
        double at[3];
        for(int k=0;k<3;k++){at[k]=(double(pose.observed[k])-nativeRegions::anchor[k])/nativeRegions::scale+nativeRegions::origin[k];if(!std::isfinite(at[k])||std::abs(at[k])>100000)return;}
        std::copy_n(at,3,link);std::copy_n(at,3,stagedLink);
        // After a control-only release MC stays where it is if Link has not
        // moved away from it meanwhile; otherwise it is put onto Link again.
        double mcAt[]{live::mc.curX,live::mc.curY,live::mc.curZ};bool beside=restage&&(live::mc.flags&kMcInWorld);
        for(int k=0;k<3;k++)beside&=std::isfinite(mcAt[k])&&std::abs(mcAt[k]-at[k])<(k==1?.75:.5);
        if(beside)std::copy_n(mcAt,3,spawn);
        else{std::copy_n(at,3,spawn);teleport=restage?(teleport<1000000?teleport+1000000:teleport+1):walking::epoch+100;}
        staged=true;
        event("{\"event\":\"native_stage\",\"epoch\":%u,\"teleport\":%u,\"spawn\":[%.12g,%.12g,%.12g],\"restage\":%d,\"kept_mc\":%d}",walking::epoch,teleport,spawn[0],spawn[1],spawn[2],int(restage),int(beside));
        return;
    }
    double position[3],velocity[3];mcMotion(position,velocity);
    nativeRegions::Key key{};int reason=0;
    if(!ready){
        double still[3]{};
        // Settled: standing, or hovering in creative flight (route-57: a release in flight never became ready again),
        // or put where Link is with nothing to stand on there: he swims, falls or glides, and the park
        // holds Minecraft's player in place, so it never lands. Route-95: Link drowned in a river and
        // came back afloat; Minecraft hung beside him and control could not be claimed again.
        static uint64_t placedSince=0;static uint32_t placedTeleport=0;
        bool placed=covered(spawn,still,now)&&live::mc.teleportAck==teleport;
        if(!placed)placedSince=0;else if(!placedSince||placedTeleport!=teleport){placedSince=now;placedTeleport=teleport;}
        if(placed&&((live::mc.flags&(kMcOnGround|kMcFlying))||now-placedSince>=kSettleMs)&&covered(position,velocity,now,&key,&reason)){
            ready=walking::ready=true;
            event("{\"event\":\"native_ready\",\"epoch\":%u,\"teleport\":%u,\"tail\":%llu,\"mc\":[%.12g,%.12g,%.12g],\"mc_seq\":%u,\"serial\":%llu}",walking::epoch,teleport,consumedTail(),position[0],position[1],position[2],live::mc.seq,++readySerial);
            return;
        }
        // Link is played natively meanwhile (control switched off, or he fell after a release in the
        // air): the stage follows him, so that Minecraft is put where he is when control is asked for.
        static uint64_t checked=0;
        if(now-checked>=250&&body::allowed()&&now-body::sampleAt.load()<=250&&!body::busy.test_and_set()){
            checked=now;auto pose=body::latest;body::busy.clear();bool moved=false;
            if(pose.actor)for(int k=0;k<3;k++){
                double at=(double(pose.observed[k])-nativeRegions::anchor[k])/nativeRegions::scale+nativeRegions::origin[k];
                // A Link who drifts (a river carried him 1.6 blocks a second in route-95) left the old
                // half block before a stage could become ready, every time. Within kStageDrift the
                // stage stands; the claim then brings Link to Minecraft's player.
                moved|=std::isfinite(at)&&std::abs(at-stagedLink[k])>kStageDrift;
            }
            if(moved){staged=false;restage=true;}
        }
        return;
    }
    // Ready, but Minecraft is not in control: when the native game has moved Link away from where
    // Minecraft stands, Minecraft is staged onto him again (not Link pulled back at the next claim).
    if(body::command.load()!=5){
        static uint64_t checked=0;
        if(now-checked>=250&&body::allowed()&&now-body::sampleAt.load()<=250&&!body::busy.test_and_set()){
            checked=now;auto pose=body::latest;body::busy.clear();bool apart=false;
            if(pose.actor)for(int k=0;k<3;k++){
                double at=(double(pose.observed[k])-nativeRegions::anchor[k])/nativeRegions::scale+nativeRegions::origin[k];
                apart|=std::isfinite(at)&&std::isfinite(position[k])&&std::abs(at-position[k])>(k==1?1.5:1.);
            }
            if(apart){staged=ready=paused=false;walking::ready=false;supportSnapshot.reset();restage=true;return;}
        }
    }
    // A waiting Minecraft reports no velocity. It continues at the velocity it had, so that is what
    // has to be covered before it may: otherwise it would stop again one tick later.
    // If that takes longer than kHoldMs (the way ahead cannot be read: not loaded, or a read that
    // keeps failing), the wait becomes SkyCraft's park: the velocity is given up and standing
    // room is enough to move again.
    if(paused&&holding&&now-pausedAt>=kHoldMs){
        holding=false;++parkPauses;parkedAt=now;
        event("{\"event\":\"native_coverage_park\",\"waited_ms\":%llu,\"at_ms\":%llu,\"qpc\":%llu}",now-pausedAt,now,qpcNow());
    }
    bool cover=covered(position,paused&&holding?heldVelocity:velocity,now,&key,&reason);
    if(!paused&&!cover){
        paused=true;++pauses;pausedAt=now;parkedAt=0;holding=lightHold.load();std::copy_n(velocity,3,heldVelocity);if(!holding)++parkPauses;
        event("{\"event\":\"native_coverage_pause\",\"reason\":%d,\"region\":[%d,%d,%d],\"mc\":[%.12g,%.12g,%.12g],\"velocity\":[%.6g,%.6g,%.6g],\"pauses\":%llu,\"hold\":%d,\"at_ms\":%llu,\"qpc\":%llu}",reason,key[0],key[1],key[2],position[0],position[1],position[2],velocity[0],velocity[1],velocity[2],pauses,int(holding),now,qpcNow());
    }else if(paused&&cover&&(!parkedAt||now-parkedAt>=kParkMs)){
        paused=false;++resumes;if(!holding)++parkResumes;
        event("{\"event\":\"native_coverage_resume\",\"mc\":[%.12g,%.12g,%.12g],\"resumes\":%llu,\"hold\":%d,\"paused_ms\":%llu,\"at_ms\":%llu,\"qpc\":%llu}",position[0],position[1],position[2],resumes,int(holding),now-pausedAt,now,qpcNow());
    }
}
// Exported triangles within 2.5 MC units horizontally and 3.5 vertically of
// the player, rebuilt when the cache changes or the player moves half a unit.
static std::shared_ptr<const NativeSupport> supportNear(const double* p){
    auto& cache=nativeRegions::cache;
    const uint64_t revision=cache.revision+nativeMovers::revision;   // either changes what is underfoot
    if(supportSnapshot&&supportSnapshot->revision==revision&&
        std::max({std::abs(p[0]-supportSnapshot->centre[0]),std::abs(p[1]-supportSnapshot->centre[1]),std::abs(p[2]-supportSnapshot->centre[2])})<.5)return supportSnapshot;
    auto s=std::make_shared<NativeSupport>();s->revision=revision;std::copy_n(p,3,s->centre);
    float lo[]{float(p[0]-2.5),float(p[1]-3.5),float(p[2]-2.5)},hi[]{float(p[0]+2.5),float(p[1]+3.5),float(p[2]+2.5)};
    for(const auto& [key,entry]:cache.entries){
        if(!entry.complete)continue;
        bool nearby=true;for(int k=0;k<3;k++)nearby&=key[k]*8-.5f<=hi[k]&&key[k]*8+8.5f>=lo[k];
        if(!nearby)continue;
        for(const auto& t:entry.triangles)if(nativeGeometry::overlaps(t,lo,hi)){std::array<float,9> v;std::copy_n(t.v,9,v.begin());s->tris.push_back(v);}
    }
    for(const auto& [body,m]:nativeMovers::movers)for(const auto& t:m.sent)if(nativeGeometry::overlaps(t,lo,hi)){std::array<float,9> v;std::copy_n(t.v,9,v.begin());s->tris.push_back(v);}
    supportSnapshot=s;return s;
}
// Movement lease. Coverage pauses MC instead of withdrawing native control,
// so a paused body stays held at MC's parked feet.
static void target(){
    if(!ready||!physicalActive.load()||physicalBlocked.load()||!live::connected||live::failed){liveBodyLease=false;return;}
    LiveTarget value;value.source=live::mc;value.session=live::session;value.bodyEpoch=nativeRegions::cache.stamp.body;value.collisionEpoch=liveCollisionEpoch.load();
    LARGE_INTEGER q;QueryPerformanceCounter(&q);value.receivedQpc=q.QuadPart;value.until=GetTickCount64()+250;
    value.scale=nativeRegions::scale;std::copy_n(nativeRegions::origin,3,value.origin);std::copy_n(nativeRegions::anchor,3,value.anchor);
    value.unbounded=true;double p[]{live::mc.curX,live::mc.curY,live::mc.curZ};value.native=supportNear(p);
    liveTarget.store(std::make_shared<const LiveTarget>(value));liveBodyLease=true;
}
// Diagnostic: on a new token, write every complete cached region (MC-space
// triangles) for offline/route planning. Worker-owned; never on a game thread.
static void dumpCache(){
    static uint64_t last=0;uint64_t token=0;std::ifstream(directory+"/cache-dump.txt")>>token;
    if(!token||token==last)return;last=token;
    std::ofstream f(directory+"/cache-dump-"+std::to_string(token)+".bin",std::ios::binary);
    uint32_t magic=0x31504D43,count=0;for(const auto& [k,e]:nativeRegions::cache.entries)count+=e.complete;
    f.write(reinterpret_cast<const char*>(&magic),4);f.write(reinterpret_cast<const char*>(&count),4);
    for(const auto& [key,entry]:nativeRegions::cache.entries){
        if(!entry.complete)continue;uint32_t n=uint32_t(entry.triangles.size());
        f.write(reinterpret_cast<const char*>(key.data()),12);f.write(reinterpret_cast<const char*>(&n),4);
        for(const auto& t:entry.triangles)f.write(reinterpret_cast<const char*>(t.v),36);
    }
    event("{\"event\":\"cache_dump\",\"token\":%llu,\"regions\":%u}",token,count);
}
static void poll(){
    int configured=0;std::ifstream(directory+"/native-stream.txt")>>configured;
    bool on=configured==1,was=nativeRegions::enabled.exchange(on);
    if(on!=was){
        walking::invalidate("native-stream-switch");walking::lastClaim=on?1:0;
        walking::ready=false;liveBodyLease=false;physicalBlocked=true;
        reset(body::lifecycleEpoch.load(),live::session,nativeRegions::world.load(),on?"stream-on":"stream-off");
    }
    if(!on)return;
    auto& cache=nativeRegions::cache;auto epoch=body::lifecycleEpoch.load();
    if(cache.stamp.body!=epoch||cache.stamp.peer!=live::session||cache.stamp.world!=nativeRegions::world.load()){
        // Body epoch alone, raised only by control-only releases since the
        // cache was stamped: keep what was read.
        if(cache.stamp.body&&cache.stamp.peer==live::session&&cache.stamp.world==nativeRegions::world.load()&&body::worldEpoch.load()<=cache.stamp.body)release(epoch);
        else reset(epoch,live::session,nativeRegions::world.load(),cache.stamp.body!=epoch?"body-epoch":cache.stamp.peer!=live::session?"peer-session":"world-identity");
    }
    if(!clearPending)publishMovers(GetTickCount64());
    publish(GetTickCount64());groundCheck();progress(GetTickCount64());dumpCache();
    if(!body::allowed()||GetTickCount64()-body::sampleAt.load()>250)return;
    if(body::busy.test_and_set())return;
    auto pose=body::latest;body::busy.clear();
    double position[3],velocity[3]{};
    for(int k=0;k<3;k++){
        position[k]=(pose.observed[k]-nativeRegions::anchor[k])/nativeRegions::scale+nativeRegions::origin[k];
        if(!std::isfinite(position[k])||std::abs(position[k])>100000)return;
    }
    if(staged)std::copy_n(position,3,link);
    if(live::connected&&!live::failed&&live::mc.tickMs>0){
        double current[]{live::mc.curX,live::mc.curY,live::mc.curZ},previous[]{live::mc.prevX,live::mc.prevY,live::mc.prevZ};
        for(int k=0;k<3;k++)if(std::isfinite(current[k])&&std::isfinite(previous[k]))velocity[k]=std::clamp((current[k]-previous[k])*1000/live::mc.tickMs,-kMaxSpeed,kMaxSpeed);
        // Minecraft moves and Link follows a frame or two behind: readiness is judged at Minecraft's
        // position, so acquisition is ordered from there too.
        bool finite=true;for(double v:current)finite&=std::isfinite(v)&&std::abs(v)<100000;
        if(ready&&finite)std::copy_n(current,3,position);
        if(paused&&holding)std::copy_n(heldVelocity,3,velocity);
    }
    {static uint64_t checked=0;if(GetTickCount64()-checked>=1000){checked=GetTickCount64();
        int path=1,hold=1;std::ifstream(directory+"/region-path.txt")>>path;std::ifstream(directory+"/coverage-hold.txt")>>hold;
        nativeRegions::pathOrder.store(path!=0);lightHold.store(hold!=0);}}
    // The harvest only tries the native world lock, and gives up when the game holds it. Its ray then
    // comes back with nothing read. Ask again in the next frame instead of at the 1 s deadline
    // (route-63: six of 2495 requests, each a pause of 0.3-0.9 s in elytra flight).
    if(cache.flight){
        auto hit=calibration::lastHit.load();uint64_t id=cache.flight->id;
        if(((hit&&hit->id==id)||calibration::unused.load()==id)&&nativeShapes::lastId.load()!=id)cache.abandon();
    }
    auto job=cache.next(position,velocity,GetTickCount64());
    if(job){
        // Diagnostic switch: decode.txt containing 0 keeps every read native.
        static bool decode=true;static uint64_t decodeChecked=0;
        if(GetTickCount64()-decodeChecked>=1000){decodeChecked=GetTickCount64();int value=1;std::ifstream(directory+"/decode.txt")>>value;decode=value!=0;}
        if(decode){auto held=cache.entries.find(job->key);nativeShapes::annotate(*job,held==cache.entries.end()?nullptr:&held->second);}
        nativeRegions::requested.store(std::make_shared<const nativeRegions::Request>(*job));
        calibration::Ray ray;ray.id=job->id;ray.epoch=epoch;ray.until=GetTickCount64()+1000;
        // The selected-update ray schedules the harvest and doubles as an
        // independent ground measurement: a golden-angle spiral of vertical
        // probes within 10 native units of Link (validation only; its result
        // never becomes collision geometry).
        static uint32_t sample=0;double angle=sample*2.399963,radius=10*std::sqrt((sample%64+.5)/64);++sample;
        std::copy_n(pose.observed,3,ray.from);ray.from[0]+=float(radius*std::cos(angle));ray.from[2]+=float(radius*std::sin(angle));
        ray.from[1]+=4;ray.delta[1]=-12;
        calibration::request.store(std::make_shared<const calibration::Ray>(ray));
        event("{\"event\":\"native_region_request\",\"id\":%llu,\"body_epoch\":%llu,\"peer\":%llu,\"generation\":%llu,\"region\":[%d,%d,%d],\"part\":%u,\"known\":%u,\"resume\":%u,\"chunk\":%u}",job->id,job->stamp.body,job->stamp.peer,job->stamp.generation,job->key[0],job->key[1],job->key[2],unsigned(job->part),unsigned(job->knownCount),job->resume.body,job->resumeChunk);
    }
    static uint64_t last=0;auto now=GetTickCount64();
    if(now-last>=1000){last=now;
        event("{\"event\":\"native_region_cache\",\"body_epoch\":%llu,\"generation\":%llu,\"entries\":%zu,\"in_flight\":%d,\"complete\":%llu,\"unknown\":%llu,\"evicted\":%llu,\"stale_rejected\":%llu,\"timeouts\":%llu,\"retained\":%llu,\"unchanged\":%llu,\"splits\":%llu,\"part_restarts\":%llu,\"reuses\":%llu,\"full_reads\":%llu,\"static_stale\":%llu,\"chunks\":%llu,\"chunk_restarts\":%llu,\"decode_ready\":%u,\"decode_captures\":%llu,\"decode_failures\":%llu,\"decode_audits\":%llu,\"decode_matched\":%llu,\"decode_mismatches\":%llu,\"decode_uses\":%llu,\"decode_triangles\":%llu,\"staged\":%d,\"ready\":%d,\"paused\":%d,\"pauses\":%llu,\"resumes\":%llu,\"lease\":%d,\"tail\":%llu,\"requests\":%llu,\"skipped\":%llu,\"parks\":%llu,\"path\":%d,\"hold\":%d,\"stale\":%llu,\"late\":%llu,\"orphan\":%llu,\"dropped\":%llu,\"dropped_region\":[%d,%d,%d],\"dropped_triangle\":[%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g]}",cache.stamp.body,cache.stamp.generation,cache.entries.size(),int(bool(cache.flight)),cache.completed,cache.unknown,cache.evicted,cache.rejected,cache.timeouts,cache.retained,cache.unchanged,cache.splits,cache.partRestarts,cache.reuses,cache.fullReads,cache.staticStale,cache.chunks,cache.chunkRestarts,nativeShapes::decodedReady(),nativeShapes::decodeCaptures,nativeShapes::decodeFailures,nativeShapes::decodeAudits,nativeShapes::decodeMatched,nativeShapes::decodeMismatches,nativeShapes::decodeUses,nativeShapes::decodeTriangles,int(staged),int(ready),int(paused),pauses,resumes,int(liveBodyLease.load()),consumedTail(),cache.serial,cache.skipped,parkPauses,int(nativeRegions::pathOrder.load()),int(lightHold.load()),cache.stale,cache.late,cache.orphan,cache.dropped,cache.droppedKey[0],cache.droppedKey[1],cache.droppedKey[2],cache.droppedTriangle[0],cache.droppedTriangle[1],cache.droppedTriangle[2],cache.droppedTriangle[3],cache.droppedTriangle[4],cache.droppedTriangle[5],cache.droppedTriangle[6],cache.droppedTriangle[7],cache.droppedTriangle[8]);
    }
}
}
