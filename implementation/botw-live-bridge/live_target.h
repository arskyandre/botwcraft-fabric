#pragma once
#include <array>
#include <atomic>
#include <deque>
#include <map>
#include <numbers>
#include "../botwcraft-fabric/protocol/botwcraft_protocol.h"
// Worker-built spatial bins. Gameplay only reads the immutable measured domain.
struct SupportDomain {
    std::map<std::pair<int,int>,std::vector<std::array<float,9>>> floors;
    bool covers(double x,double z) const {
        auto bin=floors.find({int(std::floor(x/8)),int(std::floor(z/8))});
        if(bin==floors.end())return false;
        for(const auto& v:bin->second){
            double d=(v[5]-v[8])*(v[0]-v[6])+(v[6]-v[3])*(v[2]-v[8]);
            if(d>=-1e-6)continue;
            double a=((v[5]-v[8])*(x-v[6])+(v[6]-v[3])*(z-v[8]))/d;
            double b=((v[8]-v[2])*(x-v[6])+(v[0]-v[6])*(z-v[8]))/d;
            if(std::min({a,b,1-a-b})>=-1e-6)return true;
        }
        return false;
    }
};
// Exported native triangles near the player (MC space), copied by the worker
// from complete cache entries so the game thread never touches the cache.
struct NativeSupport {std::vector<std::array<float,9>> tris;uint64_t revision=0;double centre[3]{};};
// SkyCraft TriCollider/BotwTri ground model, ported exactly: FLOOR_RADIUS .15
// with the centre, eight samples at the radius and four at half of it.
namespace support {
constexpr double kRadius=.15,kEps=1e-4; // FLOOR_RADIUS, EPS
// BotwTri.WALKABLE_NY. SkyCraft: 0.7. BOTW adaptation: 60 degrees unless
// lab/walkable-degrees.txt names another angle; the launcher passes the same
// file's angle to Minecraft (-Dbotwcraft.walkableDegrees).
static std::atomic<double> walkableNy{.5};
static double walkableFromDegrees(double degrees){return !(degrees>=30&&degrees<=75)?.5:degrees==45?.7:std::cos(degrees*std::numbers::pi/180);}
static const std::array<std::array<double,2>,13> samples=[]{
    std::array<std::array<double,2>,13> s{};
    for(int i=0;i<8;i++){double a=i*std::numbers::pi/4;s[1+i]={std::cos(a)*kRadius,std::sin(a)*kRadius};}
    for(int i=0;i<4;i++){double a=std::numbers::pi/4+i*std::numbers::pi/2;s[9+i]={std::cos(a)*kRadius*.5,std::sin(a)*kRadius*.5};}
    return s;}();
// BotwTri.heightAt: plane height over (x,z) inside the XZ projection, NaN otherwise.
static double heightAt(const std::array<float,9>& v,double x,double z,double* gradient=nullptr){
    double ax=v[0],ay=v[1],az=v[2],bx=v[3],by=v[4],bz=v[5],cx=v[6],cy=v[7],cz=v[8];
    double ux=bx-ax,uy=by-ay,uz=bz-az,wx=cx-ax,wy=cy-ay,wz=cz-az;
    double qx=uy*wz-uz*wy,qy=uz*wx-ux*wz,qz=ux*wy-uy*wx,len=std::sqrt(qx*qx+qy*qy+qz*qz);
    double nx=0,ny=1,nz=0;if(len>=1e-12){nx=qx/len;ny=qy/len;nz=qz/len;}
    if(x<std::min({ax,bx,cx})-1e-9||x>std::max({ax,bx,cx})+1e-9||z<std::min({az,bz,cz})-1e-9||z>std::max({az,bz,cz})+1e-9||std::abs(ny)<.05)return NAN;
    auto edge=[](double px,double pz,double x0,double z0,double x1,double z1){return (x1-x0)*(pz-z0)-(z1-z0)*(px-x0);};
    double d1=edge(x,z,ax,az,bx,bz),d2=edge(x,z,bx,bz,cx,cz),d3=edge(x,z,cx,cz,ax,az);
    bool neg=d1<-1e-12||d2<-1e-12||d3<-1e-12,pos=d1>1e-12||d2>1e-12||d3>1e-12;
    if(neg&&pos)return NAN;
    if(gradient){gradient[0]=-nx/ny;gradient[1]=-nz/ny;}
    return ay-(nx*(x-ax)+nz*(z-az))/ny;
}
// TriCollider.floor: highest sampled surface at most `above` over the feet.
static double floor(const NativeSupport& s,double x,double y,double z,double above,bool centreOnly,double* gradient=nullptr){
    double best=-INFINITY,limit=y+above;
    for(const auto& t:s.tris){
        if(std::min({t[1],t[4],t[7]})>limit)continue;
        for(size_t i=0;i<(centreOnly?1:samples.size());i++){
            double g[2];double h=heightAt(t,x+samples[i][0],z+samples[i][1],g);
            if(std::isfinite(h)&&h<=limit&&h>best){best=h;if(gradient){gradient[0]=g[0];gradient[1]=g[1];}}
        }
    }
    return best;
}
enum Kind {Airborne=0,Plane=1,Unexplained=2};
struct Result {int kind=Airborne;double footprint=NAN,centre=NAN,predicted=NAN,gap=0;};
// Minecraft stands on its footprint maximum; native Link rests on the surface
// under its centre. On one exported plane the two differ by exactly the
// footprint term max(gradient . sample). Only that measured geometric gap, on
// a walkable plane, is removed from the native body origin; edges, ridges,
// steep faces, MC blocks and air keep the logical MC feet. `tolerance` is
// the declared native contact tolerance.
static Result project(const NativeSupport& s,double x,double y,double z,bool grounded,double tolerance){
    Result r;if(!grounded)return r;
    r.kind=Unexplained;r.footprint=floor(s,x,y,z,tolerance,false);
    if(!std::isfinite(r.footprint)||std::abs(y-r.footprint)>tolerance)return r;
    double g[2]{};r.centre=floor(s,x,r.footprint,z,kEps,true,g);
    // Only SkyCraft-walkable planes: steeper native contact is not a centre rest.
    if(!std::isfinite(r.centre)||1/std::sqrt(1+g[0]*g[0]+g[1]*g[1])<walkableNy.load(std::memory_order_relaxed))return r;
    double rise=0;for(const auto& q:samples)rise=std::max(rise,g[0]*q[0]+g[1]*q[1]);
    r.predicted=r.centre+rise;
    if(std::abs(r.footprint-r.predicted)>tolerance)return r;
    r.kind=Plane;r.gap=r.footprint-r.centre;return r;
}
}
// Immutable MC publication. Only the existing native controller consumes it.
struct LiveTarget {
    botwcraft::proto::McState source{};
    uint64_t session=0,bodyEpoch=0,collisionEpoch=0,receivedQpc=0,until=0;
    float scale=1,origin[3]{},anchor[3]{},bounds[4]{};
    float vertical[2]{-1,3};
    std::shared_ptr<const SupportDomain> support;
    // Native stream: coverage is enforced by the worker (covers()), not by a
    // finite surveyed boundary; `native` supplies the body-origin support.
    bool unbounded=false;std::shared_ptr<const NativeSupport> native;
};
struct AppliedTarget {
    LiveTarget request;uint64_t sequence=0,qpc=0;float feet[3]{},eyeHeight=0,alpha=0;
    // Native body origin: the logical MC feet (camera/meshes keep `feet`)
    // minus the measured footprint gap when MC stands on one exported plane.
    float body[3]{};support::Result support;
    int reject=0; // first failed check when interpolation refuses (diagnostic)
    float delayMs=0,ticks=0; // tick history: how far in the past the feet were sampled, and where in the tick
};
static std::atomic<std::shared_ptr<const LiveTarget>> liveTarget;
static std::atomic<std::shared_ptr<const AppliedTarget>> appliedTarget;
static std::atomic<bool> liveBodyLease{false};
static std::atomic<uint64_t> liveCollisionEpoch{0};
static bool liveGameplayLease();
static int liveGameplayReason();
static bool liveGameplayHeld();
// A scene of the game's (report 79; in every event of the game's, the bridge keeps working
// but hands the camera and the player's control back to the game):
//  sceneOn      an event of the game's runs (gameEvents()) and lab/scene.txt is not 0;
//  sceneHold    the body is not written: from the event's first update until the control code has looked at where
//               the scene left Link (physical.h);
//  scenePresent the frame is the game's own (its camera, no selectors), Link is hidden, Steve's six parts are drawn
//               in his place and Minecraft's HUD is not: while sceneOn, and after it until control is active again.
static std::atomic<bool> sceneOn{false},sceneHold{false},scenePresent{false},sceneWanted{true};
// The Sheikah Slate Link wears and holds (the actor Item_Conductor, the sixth of the player's links) in a scene: it
// was invisible in scenes where Steve should hold it (only the slate is shown so far). lab/scene-slate.txt: 0 hidden as Link is, 1 (as built) shown in every scene,
// 2 shown only while Link has it in a hand (sceneSlateHeld, from live_scene.h). 1, since route-328: the door terminal's
// scene has a close view of the slate at Link's hip ("Hold the Sheikah Slate up to the pedestal"), empty with 2.
static std::atomic<int> sceneSlate{1};static std::atomic<bool> sceneSlateHeld{false};
// For how long a silent Minecraft is waited for with the body held (body.h, report 78); lab/stale-hold.txt, ms; 0: not at all.
static std::atomic<uint32_t> staleHoldMs{2000};
static bool interpolateTarget(const LiveTarget& t,uint64_t now,uint64_t frequency,AppliedTarget& out){
    const auto& m=t.source;out.reject=0;
    auto fail=[&](int code){out.reject=code;return false;};
    if(!frequency||!m.seq||(m.seq&1))return fail(1);
    if(m.tickQpc<=0||uint64_t(m.tickQpc)>now)return fail(2);
    if(t.receivedQpc>now||now-t.receivedQpc>frequency/4)return fail(3);
    if(now-uint64_t(m.tickQpc)>frequency/4)return fail(4);
    if(!std::isfinite(m.tickMs)||m.tickMs<=0||m.tickMs>1000)return fail(5);
    if(!(m.flags&botwcraft::proto::kMcInWorld)||t.scale<=0||t.scale>=10)return fail(6);
    double before[]={m.prevX,m.prevY,m.prevZ},after[]={m.curX,m.curY,m.curZ};
    out.request=t;out.qpc=now;out.alpha=std::clamp(float(double(now-m.tickQpc)*1000/frequency/m.tickMs),0.f,1.f);
    for(int i=0;i<3;i++){
        if(!std::isfinite(before[i])||!std::isfinite(after[i])||std::abs(after[i]-before[i])>2)return fail(7);
        out.feet[i]=t.anchor[i]+float(before[i]+out.alpha*(after[i]-before[i])-t.origin[i])*t.scale;
    }
    out.eyeHeight=(m.tickEyeO+out.alpha*(m.tickEye-m.tickEyeO))*t.scale;
    std::copy_n(out.feet,3,out.body);
    if(t.native){
        double mc[3];for(int i=0;i<3;i++)mc[i]=before[i]+out.alpha*(after[i]-before[i]);
        out.support=support::project(*t.native,mc[0],mc[1],mc[2],m.flags&botwcraft::proto::kMcOnGround,.03/t.scale);
        if(out.support.kind==support::Plane)out.body[1]=t.anchor[1]+float(mc[1]-out.support.gap-t.origin[1])*t.scale;
    }
    // Finite surveyed boundary: retain half a native unit for the capsule and
    // one response interval. Releasing before unvalidated terrain is deliberate.
    float x=out.feet[0]-t.anchor[0],z=out.feet[2]-t.anchor[2];
    if(t.support){
        double mx=x/t.scale+t.origin[0],mz=z/t.scale+t.origin[2],margin=.5/t.scale;
        for(auto offset:std::array<std::array<double,2>,5>{{{0,0},{margin,0},{-margin,0},{0,margin},{0,-margin}}})
            if(!t.support->covers(mx+offset[0],mz+offset[1]))return fail(8);
    }
    if(t.unbounded)return std::isfinite(out.eyeHeight)&&out.eyeHeight>0&&out.eyeHeight<3*t.scale&&std::isfinite(out.body[1])?true:fail(9);
    return std::isfinite(out.eyeHeight)&&out.eyeHeight>0&&out.eyeHeight<3*t.scale&&
        x>=t.bounds[0]+.5f&&x<=t.bounds[1]-.5f&&z>=t.bounds[2]+.5f&&z<=t.bounds[3]-.5f&&
        out.feet[1]>=t.anchor[1]+t.vertical[0]&&out.feet[1]<=t.anchor[1]+t.vertical[1];
}

// Minecraft's 20 Hz physics ticks interpolated on this game's clock. Ported
// from SkyCraft's Skyrim plugin (skse/src/Game.cpp): a short history of
// ticks, their stamps corrected to the tick rhythm, sampled slightly in the
// past so that the next tick has arrived before it is needed. Interpolation
// only, never extrapolation, so nothing overshoots when the velocity changes.
// Before, the single newest tick was interpolated over the 50 ms after its
// stamp; it reached the bridge a median 52 ms after that stamp (route-42), so
// three writes in four sat on the tick's end position and the body moved in
// 20 Hz steps.
// BOTW adaptation: SkyCraft bounds the delay to 4..30 ms and treats a tick
// seen 30 ms after it was due as a hitch. Skyrim runs at 60 frames a second
// or more; here both games run at 30, so a tick is regularly first seen up to
// a Minecraft frame plus a BOTW frame late. The bounds are 4..80 and 90 ms.
// Owned by the selected controller update (one game thread).
namespace tickClock {
struct Tick {botwcraft::proto::McState s;int64_t at=0;int slots=1;};
constexpr double kMinDelayMs=4,kMaxDelayMs=80,kHitchMs=90;
static std::deque<Tick> history;static int outliers=0;static double delayMs=10;
static std::array<double,40> due{};static size_t dueNext=0;static bool dueInit=false;static int64_t lastFrame=0;
static std::atomic<uint64_t> frames{0},lateFrames{0},ticksSeen{0};
static void reset(){history.clear();outliers=0;delayMs=10;dueInit=false;dueNext=0;lastFrame=0;}
// Feeds the latest state and samples the feet (MC space) and eye height at
// now - delay. `used` is the tick sampled; `ticks` is the position within it
// (above 1 with no later tick: the player stands on its end this frame).
static bool sample(const botwcraft::proto::McState& mc,int64_t now,int64_t frequency,double feet[3],double& eye,botwcraft::proto::McState& used,double& ticks){
    if(!mc.tickQpc||!(mc.tickMs>0)||frequency<=0)return false;
    const double perMs=double(frequency)/1000;const int64_t period=std::max<int64_t>(1,std::llround(double(mc.tickMs)*perMs));
    if(history.empty()||history.back().s.tickQpc!=mc.tickQpc){
        if(!history.empty()&&mc.tickQpc<history.back().s.tickQpc)reset(); // Minecraft restarted
        Tick tick{mc,mc.tickQpc,1};
        if(!history.empty()){
            auto& last=history.back();
            const int64_t n=std::llround(double(mc.tickQpc-last.at)/double(period)),err=mc.tickQpc-(last.at+n*period);
            if(n==0&&last.slots>=2){
                // Two ticks in one Minecraft frame and both seen: the first carries the second's stamp.
                last.at-=period;last.slots-=1;tick.at=last.at+period;
            }else if(n>=1&&n<=10&&std::abs(err)<period*3/10){
                tick.at=last.at+n*period+err/16;tick.slots=int(n);outliers=0; // the rhythm is exact; the stamps are noisy
            }else if(n<=10&&++outliers<3){
                tick.slots=int(std::max<int64_t>(n,1));tick.at=last.at+tick.slots*period; // one odd stamp: keep the rhythm
            }else outliers=0; // rhythm lost (a pause, a new tick rate): start from this stamp
        }
        // Was it already due on the previous frame? Then that frame had to wait for it.
        if(lastFrame){
            if(!dueInit){due.fill(delayMs-1);dueInit=true;}
            const double dueMs=double(lastFrame-tick.at)/perMs;
            if(dueMs<kHitchMs)due[dueNext++%due.size()]=dueMs;
        }
        history.push_back(tick);if(history.size()>8)history.pop_front();++ticksSeen;
    }
    // The delay follows how late ticks have been over the last 2 s: it grows
    // 2 % slower than real time and shrinks 0.2 % faster.
    const double frameMs=lastFrame?double(now-lastFrame)/perMs:0;lastFrame=now;
    if(dueInit){
        const double target=std::clamp(*std::max_element(due.begin(),due.end())+1,kMinDelayMs,kMaxDelayMs),dt=std::min(frameMs,100.)/1000;
        delayMs=target>delayMs?std::min(target,delayMs+20*dt):std::max(target,delayMs-2*dt);
    }
    const int64_t at=now-std::llround(delayMs*perMs);
    size_t i=0;for(size_t k=history.size();k-->0;)if(history[k].at<=at){i=k;break;}
    const Tick& tick=history[i];const Tick* next=i+1<history.size()?&history[i+1]:nullptr;
    ticks=double(at-tick.at)/double(period);const double t=std::clamp(ticks,0.,1.);const auto& s=tick.s;
    feet[0]=s.prevX+(s.curX-s.prevX)*t;feet[1]=s.prevY+(s.curY-s.prevY)*t;feet[2]=s.prevZ+(s.curZ-s.prevZ)*t;
    eye=s.tickEyeO+(s.tickEye-s.tickEyeO)*t;
    if(ticks>1&&next){
        // Past this tick's end and the next one known starts later: Minecraft ran a tick that was
        // never seen. Carry on from this tick's end to the next one's start.
        const auto& n=next->s;const double gap=double(next->at-(tick.at+period));
        const double u=gap>0?std::clamp(double(at-(tick.at+period))/gap,0.,1.):1;
        feet[0]=s.curX+(n.prevX-s.curX)*u;feet[1]=s.curY+(n.prevY-s.curY)*u;feet[2]=s.curZ+(n.prevZ-s.curZ)*u;
        eye=s.tickEye+(n.tickEyeO-s.tickEye)*u;
    }else if(ticks>1)++lateFrames;
    used=s;++frames;return true;
}
}
// The native stream's body target from the tick history. `latest` is the
// newest Minecraft state the caller could read (directly from the shared
// mapping when possible, otherwise the worker's copy in `t`).
static bool interpolateHistory(const LiveTarget& t,const botwcraft::proto::McState& latest,uint64_t now,uint64_t frequency,AppliedTarget& out){
    const auto& m=latest;out.reject=0;
    auto fail=[&](int code){out.reject=code;return false;};
    if(!frequency||!m.seq||(m.seq&1))return fail(1);
    if(m.tickQpc<=0||uint64_t(m.tickQpc)>now)return fail(2);
    if(t.receivedQpc>now||now-t.receivedQpc>frequency/4)return fail(3);
    if(now-uint64_t(m.tickQpc)>frequency/4)return fail(4);
    if(!std::isfinite(m.tickMs)||m.tickMs<=0||m.tickMs>1000)return fail(5);
    if(!(m.flags&botwcraft::proto::kMcInWorld)||t.scale<=0||t.scale>=10)return fail(6);
    double mc[3],eye=0,ticks=0;botwcraft::proto::McState used;
    if(!tickClock::sample(m,int64_t(now),int64_t(frequency),mc,eye,used,ticks))return fail(1);
    out.request=t;out.request.source=used;out.qpc=now;out.alpha=float(std::clamp(ticks,0.,1.));out.delayMs=float(tickClock::delayMs);out.ticks=float(ticks);
    double before[]={used.prevX,used.prevY,used.prevZ},after[]={used.curX,used.curY,used.curZ};
    for(int i=0;i<3;i++){
        if(!std::isfinite(before[i])||!std::isfinite(after[i])||!std::isfinite(mc[i])||std::abs(after[i]-before[i])>4)return fail(7);
        out.feet[i]=t.anchor[i]+float(mc[i]-t.origin[i])*t.scale;
    }
    out.eyeHeight=float(eye)*t.scale;std::copy_n(out.feet,3,out.body);
    if(t.native){
        out.support=support::project(*t.native,mc[0],mc[1],mc[2],used.flags&botwcraft::proto::kMcOnGround,.03/t.scale);
        if(out.support.kind==support::Plane)out.body[1]=t.anchor[1]+float(mc[1]-out.support.gap-t.origin[1])*t.scale;
    }
    return std::isfinite(out.eyeHeight)&&out.eyeHeight>0&&out.eyeHeight<3*t.scale&&std::isfinite(out.body[1])?true:fail(9);
}
