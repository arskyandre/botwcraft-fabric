// Rolling 8^3 region bookkeeping. Owned MC-space bytes only; no guest pointers.
#pragma once
#include <map>
#include <atomic>
#include <cmath>
#include <optional>
#include <vector>
#include <algorithm>
#include <bitset>
#include <tuple>
#include <cstring>
#include "native_geometry.h"
namespace nativeRegions {
using Key=std::array<int,3>;
struct Stamp {uint64_t body=0,peer=0,generation=0;uint32_t world=0;bool operator==(const Stamp&)const=default;};
// part: kWhole; 0..7 an octant (4 MC) of a region whose whole harvest overran
// the guest budget; 8..71 a sub-octant (2 MC): 8 + octant*8 + index.
constexpr uint8_t kWhole=0xFF;
static void partBox(const std::array<int,3>& key,uint8_t part,float* lo,float* hi){
    int base[3]{key[0]*8,key[1]*8,key[2]*8},size=8;
    if(part<8){size=4;for(int k=0;k<3;k++)base[k]+=(part>>k&1)*4;}
    else if(part<72){int o=(part-8)/8,i=(part-8)%8;size=2;for(int k=0;k<3;k++)base[k]+=(o>>k&1)*4+(i>>k&1)*2;}
    for(int k=0;k<3;k++){lo[k]=float(base[k])-.5f;hi[k]=float(base[k]+size)+.5f;}
}
// Static reuse. A fixed mesh/compound root's triangles depend only on what
// nativeShapes::rootSignature covers, so a region keeps them per root and a
// refresh names the roots it holds (`known`); the harvest skips the tree
// queries of a known root whose signature still matches.
struct Known {uint32_t body=0;uint64_t signature=0;};
constexpr size_t kKnownRoots=16;
// Resumable reads (route-21: one 2 MC cube of the shrine chamber holds over
// 1000 triangles and never fitted the 5 ms guest budget). A whole-region
// harvest that reaches the budget inside a signed root stops between two
// queries and reports what it read as chunk N; the next request names the
// root and N+1 (`resume`, `resumeChunk`) and the harvest continues there.
struct Request {uint64_t id=0;Stamp stamp;Key key{};float lo[3]{},hi[3]{};uint8_t part=kWhole,knownCount=0;std::array<Known,kKnownRoots> known{};Known resume;uint32_t resumeChunk=0;
    // Roots the worker decodes itself (native_mesh.h): the harvest reads none of their mesh triangles.
    uint8_t decodedCount=0;std::array<Known,kKnownRoots> decoded{};};
// As held by a region: canonical triangles and `read`, when they were last
// read from native. As reported by a harvest: `chunk` and `partial` describe
// a resumable read (partial: more chunks follow).
struct StaticRoot {uint32_t body=0;uint64_t signature=0;std::vector<botwcraft::proto::ColTri> triangles;uint32_t chunk=0;bool partial=false;uint64_t read=0;
    bool decoded=false; // mesh triangles from the worker's decoder (equal to the game's within float rounding)
    uint8_t audits=0;   // consecutive agreeing re-reads under this signature; each doubles the audit period (to 16x)
};
// One harvest's fixed roots: read now (fresh) or skipped as known (reused).
// `continued`: the budget ended the harvest but its progress is kept.
struct Roots {std::vector<StaticRoot> fresh;std::vector<Known> reused;bool continued=false;};
// Largest region published (40-byte triangles; the consumer has no fixed cap).
constexpr size_t kRegionTriangles=16384;
// Sorted and unique, so one root compares equal however its queries were cut.
static void canonical(std::vector<botwcraft::proto::ColTri>& tris){
    std::sort(tris.begin(),tris.end(),[](const auto& a,const auto& b){return memcmp(&a,&b,sizeof(a))<0;});
    tris.erase(std::unique(tris.begin(),tris.end(),[](const auto& a,const auto& b){return !memcmp(&a,&b,sizeof(a));}),tris.end());
}
// Refresh cadence and the oldest complete data that may still authorize
// movement. Protocol11 replaces a region in place and has no per-region
// unknown marker, so Minecraft keeps the last valid publication: ageing,
// eviction and failed refreshes pause movement instead of clearing collision.
constexpr uint64_t kRefreshMs=750,kMaxAgeMs=3000,kFlightMs=1000,kPartsWindowMs=10000;
// Refresh period of the regions next to the player's and the travel point's
// region. One harvest runs per game frame, so 27-45 such regions at kRefreshMs
// would take every harvest and still leave some older than kMaxAgeMs.
constexpr uint64_t kInnerMs=1500;
// A held root is read again at least this often, so a change the signature
// cannot see is bounded and counted (Cache::staticStale).
constexpr uint64_t kAuditMs=20000;
// Consecutive resumed harvests of one region before it counts as failed.
constexpr uint32_t kMaxContinues=64;
struct Entry {
    uint64_t updated=0,attempted=0,hash=0,publishedHead=0,stableHead=0;bool complete=false;
    std::vector<botwcraft::proto::ColTri> triangles;
    std::vector<botwcraft::proto::ColBlock> blocks;
    uint8_t publishPhase=0; // 0 pending, 1 triangles sent, 2 occupancy sent
    // Split acquisition (route-18: dense shrine regions overran the 5 ms
    // guest budget and never completed). Octants accumulate here; the region
    // becomes complete only when all eight finish within one refresh window.
    bool split=false,partsActive=false;uint8_t deep=0;std::bitset<72> done;uint64_t partsStarted=0,cycleMs=0;
    // Parts still required: an octant, or its eight sub-octants once it overran.
    bool required(uint8_t part)const{return part<8?!(deep>>part&1):part<72&&(deep>>((part-8)/8)&1);}
    int nextPart()const{for(int p=0;p<72;p++)if(required(uint8_t(p))&&!done[p])return p;return -1;}
    std::vector<botwcraft::proto::ColTri> partial;
    // Fixed roots held for this region (canonical triangles, by body), the
    // root being assembled from chunks, and whether a resumed read is under way.
    std::vector<StaticRoot> statics,partialRoots;StaticRoot assembling;
    bool continuing=false,partsOnly=false;uint32_t continues=0;
};
// The volume that has to be covered for a body at `p` moving at `v`: from one
// unit under the feet (the ground below) to the head, and on to where it is
// 0.6 s later (at most 8 MC per axis). Readiness (nativeStream::covered) and
// the order of acquisition below share it.
static void travelBox(const double* p,const double* v,double* lo,double* hi){
    for(int k=0;k<3;k++){
        double ahead=p[k]+std::clamp(v[k]*.6,-8.,8.);
        lo[k]=std::min(p[k],ahead)-(k==1?1:.3);hi[k]=std::max(p[k],ahead)+(k==1?1.8:.3);
    }
}
// Acquisition along the way (lab/region-path.txt 0: without). The regions the
// travel volume will touch within kPathSeconds at the present velocity are
// read first, in the order the body reaches them. Without it every region
// next to the travel point counts the same: 9 new ones at each region border,
// of which the travel volume needs 1-4. At 21 MC/s (sprint flight) that is
// more than the one harvest a frame can read, and a needed region is still
// missing when the body arrives (route-57: 11 of 14 pauses, all at 19-24 MC/s).
static std::atomic<bool> pathOrder{true};
constexpr double kPathSeconds=1.0;constexpr int kPathSteps=16;constexpr size_t kPathRegions=96;
static uint64_t hashTriangles(const std::vector<botwcraft::proto::ColTri>& tris){
    uint64_t h=1469598103934665603ull^tris.size();
    for(const auto& t:tris){const auto* b=reinterpret_cast<const uint8_t*>(&t);for(size_t i=0;i<sizeof(t);i++)h=(h^b[i])*1099511628211ull;}
    return h;
}
struct Cache {
    Stamp stamp;std::map<Key,Entry> entries;std::optional<Request> flight;
    uint64_t serial=0,deadline=0,skipped=0,evicted=0,rejected=0,completed=0,unknown=0,timeouts=0,retained=0,unchanged=0,revision=0,splits=0,partRestarts=0,reuses=0,fullReads=0,staticStale=0,chunks=0,chunkRestarts=0;
    // Why an answer was not taken (rejected counts them all): of another stamp, after its time, of no request in flight;
    // and the triangles left out of answers that were taken, with the last of them and its region.
    uint64_t stale=0,late=0,orphan=0,dropped=0;Key droppedKey{};float droppedTriangle[9]{};
    void reset(uint64_t body,uint64_t peer,uint32_t world=0){
        stamp={body,peer,stamp.generation+1,world};entries.clear();flight.reset();++revision;
    }
    // Control was withdrawn but the world is the same: everything read stays.
    // Only the request in flight is dropped (its answer carries the old body
    // epoch); a part or chunk it would have delivered is simply asked again.
    void rebase(uint64_t body){stamp.body=body;flight.reset();}
    // The request in flight will not be answered (its ray ended without a harvest): nothing about the
    // region is recorded, so it is simply chosen again.
    void abandon(){flight.reset();++skipped;}
    static Key key(const double* p){return {int(std::floor(p[0]/8)),int(std::floor(p[1]/8)),int(std::floor(p[2]/8))};}
    std::optional<Request> next(const double* position,const double* velocity,uint64_t now){
        for(int k=0;k<3;k++)if(!std::isfinite(position[k])||std::abs(position[k])>100000||!std::isfinite(velocity[k]))return {};
        auto center=key(position);
        // Bounded owned cache. A distant published region stays in Minecraft
        // (harmless far away); returning requires a fresh acquisition anyway.
        for(auto i=entries.begin();i!=entries.end();){
            bool distant=false;for(int k=0;k<3;k++)distant|=std::abs(i->first[k]-center[k])>3;
            if(distant){i=entries.erase(i);++evicted;++revision;}else ++i;
        }
        if(flight){
            if(now<deadline)return {};
            // Timed out: keep any previous complete data; retry after the cadence.
            auto it=entries.find(flight->key);if(it!=entries.end())it->second.attempted=now;
            flight.reset();++timeouts;
        }
        double ahead[3];for(int k=0;k<3;k++)ahead[k]=position[k]+std::clamp(velocity[k]*.6,-8.,8.);
        auto travel=key(ahead);
        std::optional<Key> chosen;std::tuple<int,uint64_t,double> best;
        // The way ahead: each region with the step at which the travel volume first touches it.
        std::array<std::pair<Key,int>,kPathRegions> path;size_t pathCount=0;
        if(pathOrder.load(std::memory_order_relaxed)){
            double fastest=std::max({std::abs(velocity[0]),std::abs(velocity[1]),std::abs(velocity[2])});
            int steps=std::min(kPathSteps,int(std::ceil(fastest*kPathSeconds/4))); // at most 4 MC (half a region) a step
            for(int i=0;i<=steps;i++){
                double q[3],lo[3],hi[3];
                for(int k=0;k<3;k++)q[k]=position[k]+velocity[k]*kPathSeconds*i/std::max(steps,1);
                travelBox(q,velocity,lo,hi);auto a=key(lo),b=key(hi);
                for(int x=a[0];x<=b[0];x++)for(int y=a[1];y<=b[1];y++)for(int z=a[2];z<=b[2];z++){
                    Key c{x,y,z};bool kept=true;for(int k=0;k<3;k++)kept&=std::abs(c[k]-center[k])<=3; // farther ones are evicted
                    if(kept&&pathCount<path.size()&&std::none_of(path.begin(),path.begin()+pathCount,[&](const auto& p){return p.first==c;}))path[pathCount++]={c,i};
                }
            }
        }
        // Full 3D neighbourhood, and the way ahead beyond it, in four tiers.
        // 0: a resumed read is finished before anything else starts; the guest
        //    keeps one continuation and its stopping point must stay valid.
        // 1: on the way, and nothing usable held for when the body gets there
        //    (missing, incomplete, or about to age out): in order of arrival.
        // 2: the regions the body can enter before they could be read again
        //    (next to the player's and the travel point's region), oldest
        //    attempt first, so none stays missing or ages out while nearer ones
        //    are read again and again (route-23: 11 pauses, each ending in MC
        //    releasing the held keys).
        // 3: the rest, nearest to the travel point first.
        auto consider=[&](const Key& candidate){
            auto it=entries.find(candidate);const Entry* e=it==entries.end()?nullptr:&it->second;
            bool beside=true,besideTravel=true;int step=-1;
            for(int k=0;k<3;k++){beside&=std::abs(candidate[k]-center[k])<=1;besideTravel&=std::abs(candidate[k]-travel[k])<=1;}
            for(size_t i=0;i<pathCount&&step<0;i++)if(path[i].first==candidate)step=path[i].second;
            const bool inner=beside||besideTravel||step>=0,focus=candidate==center||candidate==travel,busy=e&&(e->partsActive||e->continuing);
            if(e&&!busy&&now-e->attempted<(inner&&!focus&&e->complete?kInnerMs:kRefreshMs))return;
            const bool wanted=step>=0&&(!e||!e->complete||now-e->updated>=kMaxAgeMs-kInnerMs);
            double score=candidate==center?-1024:0;
            for(int k=0;k<3;k++){double d=candidate[k]*8+4-ahead[k];score+=d*d;}
            std::tuple<int,uint64_t,double> rank{e&&e->continuing?0:wanted?1:inner?2:3,wanted?uint64_t(step):inner&&e?e->attempted:0,score};
            if(!chosen||rank<best){best=rank;chosen=candidate;}
        };
        for(int x=-2;x<=2;x++)for(int y=-2;y<=2;y++)for(int z=-2;z<=2;z++)consider({center[0]+x,center[1]+y,center[2]+z});
        for(size_t i=0;i<pathCount;i++){
            bool outside=false;for(int k=0;k<3;k++)outside|=std::abs(path[i].first[k]-center[k])>2;
            if(outside)consider(path[i].first);
        }
        if(!chosen)return {};
        Request r;r.id=(1ull<<63)|++serial;r.stamp=stamp;r.key=*chosen;
        for(int k=0;k<3;k++){r.lo[k]=float(r.key[k]*8)-.5f;r.hi[k]=float(r.key[k]*8+8)+.5f;}
        if(auto it=entries.find(*chosen);it!=entries.end()){
            auto& e=it->second;
            // Audits are staggered so neighbouring regions do not re-read together.
            uint64_t audit=kAuditMs+((uint32_t(r.key[0])*73856093u^uint32_t(r.key[1])*19349663u^uint32_t(r.key[2])*83492791u)&7)*1000;
            if(!e.partsActive&&!e.partsOnly&&!(e.split&&e.statics.empty())){
                // One whole request that may skip the roots held here, except
                // those due for their audit, and resumes a read in progress.
                for(const auto& root:e.statics)if(r.knownCount<kKnownRoots&&now-root.read<(audit<<root.audits))r.known[r.knownCount++]={root.body,root.signature};
                if(e.assembling.body){r.resume={e.assembling.body,e.assembling.signature};r.resumeChunk=e.assembling.chunk;}
            }else if(e.split){
                if(e.done.none()||now-e.partsStarted>=kPartsWindowMs){if(e.done.any())++partRestarts;e.done.reset();e.partial.clear();e.partialRoots.clear();e.partsStarted=now;}
                e.partsActive=true;r.part=uint8_t(e.nextPart());partBox(r.key,r.part,r.lo,r.hi);
            }
        }
        flight=r;deadline=now+kFlightMs;return r;
    }
    // `triangles` excludes those of `roots.fresh`, which stay with their root.
    bool finish(const Request& r,bool complete,std::vector<botwcraft::proto::ColTri> triangles,uint64_t now,bool overBudget=false,Roots roots={}){
        if(r.stamp!=stamp||!flight||flight->id!=r.id||flight->key!=r.key||now>=deadline){++rejected;++(r.stamp!=stamp?stale:!flight||flight->id!=r.id||flight->key!=r.key?orphan:late);return false;}
        if(triangles.size()>kRegionTriangles){complete=false;triangles.clear();}
        // A triangle without area or without a finite place collides with nothing: it is left out. The whole answer was
        // refused for one of them before, and the request then waited out its second and was made again, for as long as
        // the body stayed: the regions around were read once in a second and a half, and the one itself never. In
        // route-248, a play session by the Stasis shrine at night: Minecraft's player was put there by the park and
        // held for ground that never came, /tp did not get him out, and control could not be taken again.
        auto sound=[&](auto& list){
            size_t before=list.size();
            std::erase_if(list,[&](const auto& t){if(nativeGeometry::valid(t))return false;droppedKey=r.key;std::copy_n(t.v,9,droppedTriangle);return true;});
            dropped+=before-list.size();
        };
        sound(triangles);for(auto& f:roots.fresh)sound(f.triangles);
        if(r.part!=kWhole){
            auto& e=entries[r.key];flight.reset();
            if(!complete&&overBudget&&r.part<8&&e.split&&!(e.deep>>r.part&1)){
                // This octant alone overruns: its eight sub-octants replace it.
                e.deep|=uint8_t(1u<<r.part);++splits;++unknown;return true;
            }
            // Parts are full reads. A root whose signature differs between two
            // parts was read in two states: the set restarts.
            bool moved=!roots.reused.empty();
            for(const auto& f:roots.fresh){moved|=f.partial||f.chunk;for(const auto& held:e.partialRoots)moved|=held.body==f.body&&held.signature!=f.signature;}
            if(!complete||moved||!e.split||!e.required(r.part)||e.done[r.part]){e.done.reset();e.partial.clear();e.partialRoots.clear();e.partsActive=false;e.attempted=now;++unknown;if(e.complete)++retained;return true;}
            e.done.set(r.part);e.partial.insert(e.partial.end(),triangles.begin(),triangles.end());
            for(auto& f:roots.fresh){
                auto held=std::find_if(e.partialRoots.begin(),e.partialRoots.end(),[&](const auto& x){return x.body==f.body;});
                if(held==e.partialRoots.end())e.partialRoots.push_back(std::move(f));
                else held->triangles.insert(held->triangles.end(),f.triangles.begin(),f.triangles.end());
            }
            if(e.nextPart()>=0)return true;
            // All octants: drop exact duplicates of triangles spanning octants.
            std::vector<botwcraft::proto::ColTri> whole;
            std::map<std::array<float,9>,bool> seen;
            float lo[3],hi[3];for(int k=0;k<3;k++){lo[k]=float(r.key[k]*8)-.5f;hi[k]=float(r.key[k]*8+8)+.5f;}
            for(const auto& t:e.partial){std::array<float,9> v;std::copy_n(t.v,9,v.begin());if(seen.emplace(v,true).second&&nativeGeometry::overlaps(t,lo,hi))whole.push_back(t);}
            Roots all;all.fresh=std::move(e.partialRoots);
            for(auto& f:all.fresh)std::erase_if(f.triangles,[&](const auto& t){return !nativeGeometry::overlaps(t,lo,hi);});
            e.done.reset();e.partial.clear();e.partialRoots.clear();e.partsActive=false;triangles=std::move(whole);
            e.cycleMs=std::min<uint64_t>(now-e.partsStarted,kPartsWindowMs);
            Request full=r;full.part=kWhole;full.knownCount=0;full.resume={};flight=full; // finish the region below as one acquisition
            return finish(full,true,std::move(triangles),now,false,std::move(all));
        }
        if(!complete&&overBudget&&!roots.continued){
            // Next acquisition of this region proceeds by octants immediately;
            // a region that overruns again after a parts cycle stays on parts.
            auto& e=entries[r.key];if(!e.split){e.split=true;++splits;}else e.partsOnly=true;
            e.done.reset();e.partial.clear();e.partialRoots.clear();e.partsActive=true;e.continuing=false;e.continues=0;e.assembling={};
            e.attempted=now;flight.reset();++unknown;if(e.complete)++retained;return true;
        }
        auto& entry=entries[r.key];
        // Roots read by this harvest. A resumable read arrives as chunks and
        // counts once its last chunk joins the earlier ones in order.
        std::vector<uint32_t> read;bool lost=false;
        for(auto& f:roots.fresh){
            if(f.chunk||f.partial){
                auto& a=entry.assembling;
                if(!f.chunk){if(a.body)++chunkRestarts;a={f.body,f.signature,{},0};}
                if(a.body!=f.body||a.signature!=f.signature||a.chunk!=f.chunk||a.triangles.size()+f.triangles.size()>kRegionTriangles){a={};lost=true;++chunkRestarts;continue;}
                a.triangles.insert(a.triangles.end(),f.triangles.begin(),f.triangles.end());++a.chunk;++chunks;
                if(f.partial)continue;
                f.triangles=std::move(a.triangles);a={};
            }else if(entry.assembling.body==f.body)entry.assembling={};
            canonical(f.triangles);
            // Held at once, whatever else this harvest lacked: the read of this
            // root is complete under its signature. Re-reading an unchanged
            // signature must give the held triangles (the audit).
            auto held=std::find_if(entry.statics.begin(),entry.statics.end(),[&](const auto& x){return x.body==f.body;});
            const bool again=held!=entry.statics.end()&&held->signature==f.signature;
            const bool stale=again&&held->decoded==f.decoded&&(held->triangles.size()!=f.triangles.size()||
                (!f.triangles.empty()&&memcmp(held->triangles.data(),f.triangles.data(),f.triangles.size()*sizeof(f.triangles[0]))));
            staticStale+=stale;
            StaticRoot root{f.body,f.signature,std::move(f.triangles),0,false,now,f.decoded};
            root.audits=again&&!stale?uint8_t(std::min(held->audits+1,4)):0;
            if(held!=entry.statics.end())*held=std::move(root);else entry.statics.push_back(std::move(root));
            read.push_back(f.body);
        }
        if(lost)complete=false;
        entry.continuing=!complete&&roots.continued&&!lost&&++entry.continues<kMaxContinues;
        if(!entry.continuing)entry.continues=0;
        // The region: every root read now or skipped under a held signature.
        std::vector<StaticRoot> statics;
        if(complete){
            for(const auto& root:entry.statics)if(std::find(read.begin(),read.end(),root.body)!=read.end())statics.push_back(root);
            for(const auto& k:roots.reused){
                auto held=std::find_if(entry.statics.begin(),entry.statics.end(),[&](const auto& x){return x.body==k.body&&x.signature==k.signature;});
                if(held==entry.statics.end()||std::find(read.begin(),read.end(),k.body)!=read.end()){complete=false;break;}
                statics.push_back(*held);
            }
            std::sort(statics.begin(),statics.end(),[](const auto& a,const auto& b){return a.body<b.body;});
            for(const auto& root:statics)triangles.insert(triangles.end(),root.triangles.begin(),root.triangles.end());
            if(triangles.size()>kRegionTriangles)complete=false;
        }
        std::vector<botwcraft::proto::ColBlock> blocks;
        if(complete&&!nativeGeometry::voxelize(r.key,triangles,blocks))complete=false;
        entry.attempted=now;flight.reset();
        if(!complete){
            // Never replace valid published data with an unknown result. A
            // resumed read in progress is neither: it is asked for again at once.
            if(entry.continuing)return true;
            ++unknown;if(entry.complete)++retained;return true;
        }
        ++completed;
        if(roots.reused.empty())++fullReads;else ++reuses;
        entry.statics=std::move(statics);entry.assembling={}; // roots that left the region go with it
        auto hash=hashTriangles(triangles);
        if(entry.complete&&entry.hash==hash){entry.updated=now;++unchanged;return true;}
        entry.updated=now;entry.complete=true;entry.hash=hash;entry.triangles=std::move(triangles);
        entry.blocks=std::move(blocks);entry.publishPhase=0;++revision;
        return true;
    }
    // Readiness: every region touching the volume holds complete data no older
    // than kMaxAgeMs, and Minecraft has consumed a publication of it.
    // Reason: 1 invalid volume, 2 missing, 3 incomplete, 4 older than
    // kMaxAgeMs, 5 not yet published, 6 published but not consumed by MC.
    bool covers(const double* lo,const double* hi,uint64_t now,uint64_t consumed,Key* failed=nullptr,int* reason=nullptr)const{
        auto fail=[&](Key k,int r){if(failed)*failed=k;if(reason)*reason=r;return false;};
        for(int k=0;k<3;k++)if(!std::isfinite(lo[k])||!std::isfinite(hi[k])||std::abs(lo[k])>100000||std::abs(hi[k])>100000||lo[k]>hi[k]||hi[k]-lo[k]>32)return fail({},1);
        auto a=key(lo),b=key(hi);
        for(int x=a[0];x<=b[0];x++)for(int y=a[1];y<=b[1];y++)for(int z=a[2];z<=b[2];z++){
            auto it=entries.find({x,y,z});
            if(it==entries.end())return fail({x,y,z},2);
            if(!it->second.complete)return fail({x,y,z},3);
            // A split region's full refresh takes cycleMs; freshness is relative to it.
            if(now-it->second.updated>=kMaxAgeMs+it->second.cycleMs)return fail({x,y,z},4);
            if(!it->second.stableHead)return fail({x,y,z},5);
            if(it->second.stableHead>consumed)return fail({x,y,z},6);
        }
        return true;
    }
};
static Cache cache; // Existing host worker is the sole owner.
static std::atomic<std::shared_ptr<const Request>> requested;
static std::atomic<bool> enabled{false};
static std::atomic<uint32_t> world{0};
static constexpr float scale=1.3f,anchor[3]{-1027.16809f,251.188492f,1812.87195f},origin[3]{0,100,0};
}
namespace nativeStream {static void configure(botwcraft::proto::BotwState&);static void target();}
