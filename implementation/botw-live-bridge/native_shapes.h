// US208 diagnostic acquisition, not a complete geometry/coverage source.
// Hook provenance: physics-leads-03/04/05.json. No Switch address is executed.
#pragma once
#include <array>
#include <set>
namespace walls {static bool mine(const float* at);   // the bridge's own boxes for Minecraft's blocks (walls.h): not harvested
// The filter the bridge's walls are given (entity +2C) where the harvest meets them, which is within a harvest
// of their making. Without it a Guardian's beam went through Minecraft's blocks. An AirWall's own
// filter is 8E2E3800 (route-216): layer 7, and the kinds 3, 4, 5, 9, 10, 11 and 13 pass (physDefines.h GroundHit:
// the camera's two, the attacks' two, IK, Grudge, the line of sight). Route-217, Steve in a box of stone ten
// blocks from a Guardian that hits every 6.3 s: with 8E0E0800 (the attacks and the sight no longer pass, still
// layer 7) it hits as before, four times in 25 s; with 900E0800 or 90000000 (layer 8, the ground's) not once in
// 25 s each; with 8E2E3800 again it hits again. So the game's rays for these do not ask layer 7 at all.
// 90000000 is what the game's own ground meshes carry (report 64: for everything). lab/wall-filter.txt: another
// filter in hexadecimal; 0 leaves the AirWall's own.
constexpr uint32_t kFilter=0x90000000;
static std::atomic<uint32_t> filter{kFilter},refiltered{0};}
// Where a thing flies that Steve struck (report 72). A thing held by Stasis could be
// sent sideways but not up or down, and it is to follow Steve's look. The game makes the launch
// by a rule of its own (report 71: not the weapon's movement alone). So the launch is left to the game and
// turned afterwards: the swing names the place of what it struck and its own line (aim); the harvest's list of
// bodies, which the game's thread walks every frame, finds the body that stands there (not a fixed one) and,
// when that body starts to move, gives its velocity the line's direction and keeps its speed. A body's
// velocity is three floats 0xC0 after its transform (Havok's hkpMotion: the motion state at +10, 0xB0 long,
// the inverse inertia and mass, then the linear velocity). Only a body that stood still for kHeldMs after the
// first blow is turned: that is what Stasis holds. A loose thing moves with the blow itself and is left to the
// game. Observed (route-263, an iron box held by Stasis, five blows): the game's own launch was away from Link
// with a rise of 12 degrees whatever the swing's line (so a player could steer it sideways only); turned, it
// left within 10 degrees of the line. lab/look-throw.txt 0: not done.
namespace lookThrow {
struct State {bool wanted=false;float at[3]{},line[3]{};uint32_t transform=0;bool still=false;uint64_t until=0,since=0;};
constexpr uint64_t kHeldMs=500;
static std::mutex mutex;static State state;static std::atomic<bool> on{true};
constexpr uint64_t kStoredMs=20000;static float storedLine[3]{};static uint64_t storedUntil=0;   // the last swing's line, for stored() below
static void aim(const float* at,const float* line){
    if(!on.load())return;
    std::lock_guard lock(mutex);bool same=state.transform&&std::abs(state.at[0]-at[0])+std::abs(state.at[1]-at[1])+std::abs(state.at[2]-at[2])<.3f;
    uint32_t keep=same?state.transform:0;bool still=same&&state.still;uint64_t now=GetTickCount64(),since=same&&state.since?state.since:now;
    std::copy_n(line,3,storedLine);storedUntil=now+kStoredMs;
    state={!keep,{at[0],at[1],at[2]},{line[0],line[1],line[2]},keep,still,now+15000,since};
    event("{\"event\":\"look_throw\",\"state\":\"aimed\",\"at\":[%.3f,%.3f,%.3f],\"line\":[%.3f,%.3f,%.3f],\"body_known\":%d}",at[0],at[1],at[2],line[0],line[1],line[2],int(keep!=0));
}
// What Stasis itself has stored (report 73). Observed (route-267, the memory of the held box and of what the rune's
// statics point to, before a blow, after it and after the release): the static at 1046380C, beside the rune
// manager's, points to an object that holds, 0x54 in, a word that is 1 from a blow on the held thing until its
// release, and 0x2BC in, three floats that the blow sets and that point where the thing then flies (within 3
// degrees; -534600, 310600, 1030000 for a launch of 14.3 m/s). A video of a play session: the game's own
// arrow on the held thing still pointed up whatever Steve looks at, because only the velocity was turned, at the
// release. So these three floats are given the swing's line, their length kept, in every harvest while the word
// is 1 and a swing of Steve's has struck within kStoredMs: the arrow and the launch are then the game's own.
constexpr uint32_t kStasisObject=0x1046380C,kStasisStruck=0x54,kStasisStored=0x2BC;
// The word at +0x54 counts the blows of the hold (a play session, route-269: with a test for 1 only the first
// blow of each hold was answered, and a hold of several blows left by the game's rule, rise 14).
static void stored(){
    std::lock_guard lock(mutex);
    if(!on.load()||!storedUntil||GetTickCount64()>storedUntil||!::body::readable(kStasisObject,4))return;
    uint32_t object=::body::u32(kStasisObject);
    if(object<0x10000000||!::body::readable(object+kStasisStruck,4)||!::body::readable(object+kStasisStored,12)||!::body::u32(object+kStasisStruck)||::body::u32(object+kStasisStruck)>4096)return;
    float v[3];for(int i=0;i<3;i++){uint32_t raw=::body::u32(object+kStasisStored+i*4);memcpy(&v[i],&raw,4);}
    float length=std::sqrt(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]);
    if(!std::isfinite(length)||length<1||length>1e9f)return;
    if((v[0]*storedLine[0]+v[1]*storedLine[1]+v[2]*storedLine[2])/length>.99996f)return;   // within half a degree already
    for(int i=0;i<3;i++){float w=storedLine[i]*length;uint32_t raw;memcpy(&raw,&w,4);::body::put32(object+kStasisStored+i*4,raw);}
    event("{\"event\":\"look_throw\",\"state\":\"stored\",\"object\":%u,\"length\":%.1f,\"was\":[%.4f,%.4f,%.4f],\"line\":[%.4f,%.4f,%.4f]}",object,length,v[0]/length,v[1]/length,v[2]/length,storedLine[0],storedLine[1],storedLine[2]);
}
// From the harvest's list (the game's thread): a body that is not fixed, where its transform is and where it stands.
static void body(uint32_t transform,const float* at){
    std::lock_guard lock(mutex);auto& s=state;
    if(!s.wanted&&!s.transform)return;
    if(GetTickCount64()>s.until){if(s.transform||s.wanted)event("{\"event\":\"look_throw\",\"state\":\"given-up\",\"body_known\":%d,\"still\":%d}",int(s.transform!=0),int(s.still));s={};return;}
    if(s.wanted){
        if(std::abs(at[0]-s.at[0])>.5f||std::abs(at[1]-s.at[1])>.5f||std::abs(at[2]-s.at[2])>.5f)return;
        s.transform=transform;s.wanted=false;s.still=false;
    }
    if(transform!=s.transform||!::body::readable(transform+0xC0,12))return;
    float v[3];for(int i=0;i<3;i++){uint32_t raw=::body::u32(transform+0xC0+i*4);memcpy(&v[i],&raw,4);}
    float speed=std::sqrt(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]);
    if(!std::isfinite(speed)||speed>400){event("{\"event\":\"look_throw\",\"state\":\"not-a-velocity\",\"transform\":%u}",transform);s={};return;}
    if(speed<.5f){if(!s.still){s.still=true;event("{\"event\":\"look_throw\",\"state\":\"still\",\"transform\":%u,\"at\":[%.3f,%.3f,%.3f]}",transform,at[0],at[1],at[2]);}return;}
    if(speed<2)return;
    if(!s.still||GetTickCount64()-s.since<kHeldMs){event("{\"event\":\"look_throw\",\"state\":\"moved-with-the-blow\",\"transform\":%u,\"speed\":%.3f,\"was_still\":%d}",transform,speed,int(s.still));s={};return;}
    for(int i=0;i<3;i++){float w=s.line[i]*speed;uint32_t raw;memcpy(&raw,&w,4);::body::put32(transform+0xC0+i*4,raw);}
    event("{\"event\":\"look_throw\",\"state\":\"turned\",\"transform\":%u,\"speed\":%.3f,\"was\":[%.3f,%.3f,%.3f],\"line\":[%.3f,%.3f,%.3f],\"was_still\":%d}",transform,speed,v[0],v[1],v[2],s.line[0],s.line[1],s.line[2],int(s.still));
    s={};
}
}
namespace nativeShapes {
struct Block { uint32_t address=0,size=0; std::array<uint8_t,256> bytes{}; };
struct Triangle {uint32_t body=0,key=0;float v[9]{},radius=0;};
struct QueryBox {float lo[3]{},hi[3]{};uint32_t depth=0;};
// Non-triangle bv-tree child (e.g. a convex piece) read by its own virtual
// getAabb in the root body's world transform: SkyCraft's per-child fallback.
struct LeafBox {uint32_t body=0,key=0,vtable=0;uint8_t type=0;float lo[3]{},hi[3]{};};
struct UnknownLeaf {uint32_t body=0,key=0,vtable=0,block=~0u;uint8_t type=0,reason=0;};
// A convex piece of a body that is neither a mesh nor one exact box (a list of pieces, a hull of points, a box
// or a hull under a transform): its corner points in the game's world, copied in the harvest. The worker makes
// the hull's faces of them. Before route-195 such a body was one upright box around all of it: the shrine of
// the Eastern Abbey, a list of twelve pieces, was a solid box of 7.9 x 6.1 x 7.5 blocks, door and all.
struct Piece {uint32_t body=0,first=0,count=0,key=~0u;};   // key: a compound's child, ~0 the body's own shape
// A body or child the pieces could not be read of (it stays a box): reason 1 a read, 2 a kind not read yet,
// 3 a hull's points do not fill its own bounds, 4 a list's pieces do not fill its bounds, 5 no room, 6 too deep,
// 7 a bad transform, 8 the points lie outside the bounds the game gives, 9 the harvest's time.
struct PieceFailure {uint32_t body=0,shape=0,vtable=0;uint8_t type=0,reason=0;};
constexpr uint32_t kPiecePoints=256;   // the most points of one hull
static std::atomic<bool> convexPieces{true};   // lab/convex-pieces.txt 0: the box of the bounds, as before
static std::atomic<bool> childFilters{true};   // lab/child-filters.txt 0: every child of an included body is solid, as before
enum class Kind:uint8_t {Excluded,Compound,Mesh,Height,Fallback,Unsupported};
struct Candidate {
    uint32_t body=0,island=0,group=0,shape=0,vtable=0,filter=0,aabbFn=0;
    Block shapeBytes,transform;
    float lo[3]{},hi[3]{};
    bool bounded=false,overlap=false,included=false;
    Kind kind=Kind::Excluded;
    uint64_t signature=0; // rootSignature(); zero: read every time
    bool reused=false;    // known to the request under this signature: not queried
    // Read state of a root this harvest: `done` every query answered without
    // a fault; `partial` stopped at the budget, to be resumed as chunk+1.
    bool done=false,partial=false;uint32_t chunk=0;
    bool decoded=false; // the worker decodes this root's mesh triangles; only other children are read here
    bool pieces=false;  // its convex pieces are in the snapshot: no box is made of its bounds
};
struct TerrainGrid {
    uint32_t shape=0,reader=0,count=0,read=0,available=0;
    int x=0,z=0,width=0,depth=0;
    float offset[2]{},spacing=0;
    bool complete=false;
    std::array<float,2048> height{};
    std::array<uint8_t,2048> valid{};
};
struct Snapshot {
    uint64_t id=0,epoch=0,qpc=0,ticks=0; uint32_t layer=0,system=0,world=0,count=0;
    uint32_t entityStart=0,entityTotal=0,selected=0,keyCount=0,leaves=0;
    uint32_t heightShape=0,heightReader=0; int heightAvailable=-1; float height=0;
    uint32_t keyBlock=~0u; std::array<uint32_t,4> leafBlocks{},leafKeys{};
    float queryLo[3]{},queryHi[3]{};
    uint32_t ownedLockDepth=0;
    std::optional<nativeRegions::Request> region;
    uint32_t queries=0,saturatedQueries=0,triangleCount=0,unknownLeaves=0,maxKeys=0,keyFaults=0,keyFaultReason=0,overCapacity=0;
    uint32_t leafBoxCount=0,unknownCount=0;
    std::array<LeafBox,512> leafBoxes{};
    std::array<UnknownLeaf,16> unknown{};
    uint32_t pieceCount=0,piecePointCount=0,pieceFailureCount=0,pieceFailures=0;
    std::array<Piece,1024> pieces{};
    std::array<float,3*32768> piecePoints{};
    std::array<PieceFailure,16> pieceFailed{};
    uint32_t reusedRoots=0,queriedRoots=0,doneRoots=0,decodedRoots=0,droppedKeys=0;
    bool continued=false; // budget reached with resumable progress kept
    bool compoundComplete=false,budgetExceeded=false;
    bool inventoryComplete=false;
    uint32_t fixedBody=0,emptyFixed=0,inventoryFailure=0,failureAddress=0;
    uint32_t islands[3]{},bodies[3]{},candidateCount=0,boundsRead=0,unsupported=0;
    // Policy counters: excluded layers, eligible AABB fallbacks, included
    // bodies whose bounds could not be established (coverage unavailable).
    uint32_t excluded=0,fallbacks=0,unboundable=0,meshRoots=0,heightfields=0;
    std::array<Candidate,1024> candidates{};
    std::array<Triangle,4096> triangles{};
    TerrainGrid terrain;
    // All copies are made during the native RayCast world-lock scope. Workers
    // consume these bytes only; they never follow guest pointers.
    std::array<Block,128> blocks{};
};
static Snapshot pending;
static std::atomic<uint32_t> state{0}; // 0 free, 1 copying, 2 owned bytes ready
// Set when a harvest has ended: the worker takes it up at once instead of at its next 20 ms pass.
static const HANDLE harvested=CreateEventW(nullptr,FALSE,FALSE,nullptr);
static std::atomic<uint64_t> lastId{0};
static uint32_t boundsCursor=0,rootCursor=0,currentRoot=0;
static std::array<QueryBox,64> queryStack{};
static QueryBox queryBox;
static size_t queryPending=0;
static bool rootOpen=false; // currentRoot has been started and not yet closed
// The one resumable read: the unanswered queries of a signed root, valid only
// for the request that names the same region, root, signature and chunk.
struct Continuation {
    uint64_t generation=0;std::array<int,3> key{};uint32_t body=0,chunk=0;uint64_t signature=0;
    size_t count=0;std::array<QueryBox,64> stack{};
};
static Continuation continuation;
// Owned codecave query buffer (pack ShapeKeyBuffer). US208 041353AC stores
// keys[count++] and stops when count reaches capacity (query-capacity-20b/d),
// so count==capacity is ambiguous and still subdivides. Canaries after the
// buffer detect any write beyond the passed capacity.
constexpr uint32_t kKeyCapacity=256,kKeyCanaries=16;
static uint32_t keyBuffer=0;
static uint32_t currentKeys[kKeyCapacity]{};
static constexpr uint32_t canary(uint32_t i){return 0x4B455953u^(i*0x9E3779B9u);}
static bool keyBufferValid(uint32_t address){
    return address>=0x01800000&&address<0x10000000&&!(address&3)&&body::readable(address,(kKeyCapacity+kKeyCanaries)*4,0x01800000);
}
// Per-harvest (body,key) dedup; open addressing, cleared at harvest start.
static std::array<uint64_t,8192> leafSeen{};
static bool leafFirst(uint32_t bodyAddress,uint32_t key){
    uint64_t value=(uint64_t(bodyAddress)<<32|key)+1;
    for(uint32_t i=uint32_t((value*0x9E3779B97F4A7C15ull)>>51),n=0;n<leafSeen.size();i=(i+1)&(leafSeen.size()-1),n++){
        if(leafSeen[i]==value)return false;
        if(!leafSeen[i]){leafSeen[i]=value;return true;}
    }
    return false;
}
static LARGE_INTEGER frequency{};
// Each PPC invocation owns this storage in its 0x900-byte guest stack frame.
// Nested raycasts must restore their own paired FPR/special state and must not
// complete or overwrite an outer harvest. No allocation or TLS save-slot stack.
struct MachineState {
    uint32_t magic=0,frame=0;double fpr[64]; uint32_t fpscr,xer,gqr[8]; uint8_t ca,lsqe,pse;
};
static_assert(sizeof(MachineState)<=0x300);
static thread_local uint32_t harvestFrame=0;
struct WorldLock {uint32_t wrapper=0,depth=0;};
static thread_local std::array<WorldLock,4> worldLocks{};
static void lockEntered(CPU* c){
    body::ret(c);
    const auto wrapper=c->gpr[31];
    for(auto& lock:worldLocks)if(lock.wrapper==wrapper){++lock.depth;return;}
    for(auto& lock:worldLocks)if(!lock.depth){lock={wrapper,1};return;}
    // Untracked lock nesting cannot authorize collection.
}
static void lockLeft(CPU* c){
    body::ret(c);
    for(auto& lock:worldLocks)if(lock.wrapper==c->gpr[31]){if(lock.depth)--lock.depth;return;}
}
static bool harvesting(CPU* c){return harvestFrame&&harvestFrame==c->gpr[1];}
static bool machine(CPU* c,bool restore){
    uint32_t address=c->gpr[1]+0x600;
    if(address<c->gpr[1]||!body::readable(address,sizeof(MachineState),0x10000))return false;
    MachineState s{};
    if(restore){
        memcpy(&s,body::base+address,sizeof(s));
        if(s.magic!=0x53485045||s.frame!=c->gpr[1])return false;
        memcpy(c->fpr,s.fpr,sizeof(s.fpr));c->fpscr=s.fpscr;c->spr.xer=s.xer;
        memcpy(c->spr.gqr,s.gqr,sizeof(s.gqr));c->ca=s.ca;c->lsqe=s.lsqe;c->pse=s.pse;
    }else{
        s.magic=0x53485045;s.frame=c->gpr[1];
        memcpy(s.fpr,c->fpr,sizeof(s.fpr));s.fpscr=c->fpscr;s.xer=c->spr.xer;
        memcpy(s.gqr,c->spr.gqr,sizeof(s.gqr));s.ca=c->ca;s.lsqe=c->lsqe;s.pse=c->pse;
        memcpy(body::base+address,&s,sizeof(s));
    }
    return true;
}
static bool copy(Snapshot& s,uint32_t address,uint32_t size,uint32_t minimum=0x10000000) {
    if(s.count==s.blocks.size() || !size || size>256 || !body::base || address<minimum ||
        (address&3) || uint64_t(address)+size>0xA0000000)return false;
    Block& b=s.blocks[s.count]; SIZE_T copied=0;
    if(!ReadProcessMemory(GetCurrentProcess(),body::base+address,b.bytes.data(),size,&copied)||copied!=size)return false;
    b.address=address;b.size=size;++s.count;return true;
}
static uint32_t word(const Block& b,uint32_t offset) {
    uint32_t value;memcpy(&value,b.bytes.data()+offset,4);return __builtin_bswap32(value);
}
static float real(const Block& b,uint32_t offset){auto bits=word(b,offset);float f;memcpy(&f,&bits,4);return f;}
static bool readBlock(Block& b,uint32_t address,uint32_t size){
    if(!body::base||!size||size>b.bytes.size()||address<0x10000000||(address&3)||uint64_t(address)+size>0xA0000000)return false;
    SIZE_T copied=0;b.address=address;b.size=size;
    return ReadProcessMemory(GetCurrentProcess(),body::base+address,b.bytes.data(),size,&copied)&&copied==size;
}
static bool rigidTransform(const Block& xf){
    if(xf.size!=64)return false;
    for(int i=0;i<16;i++)if(!std::isfinite(real(xf,i*4)))return false;
    for(int i=0;i<3;i++)for(int j=i;j<3;j++){
        float dot=0;for(int k=0;k<3;k++)dot+=real(xf,i*16+k*4)*real(xf,j*16+k*4);
        if(std::abs(dot-(i==j?1.f:0.f))>.001f)return false;
    }
    float cross[3];float a[]{real(xf,0),real(xf,4),real(xf,8)},b[]{real(xf,16),real(xf,20),real(xf,24)};
    nativeGeometry::cross(a,b,cross);
    float determinant=0;for(int k=0;k<3;k++)determinant+=cross[k]*real(xf,32+k*4);
    return std::abs(determinant-1)<.001f;
}
static QueryBox localQuery(const Block& xf,const float* lo,const float* hi){
    QueryBox box;std::fill_n(box.lo,3,INFINITY);std::fill_n(box.hi,3,-INFINITY);
    for(int corner=0;corner<8;corner++)for(int k=0;k<3;k++){
        float x=0;for(int j=0;j<3;j++)x+=real(xf,k*16+j*4)*((corner&(1<<j)?hi[j]:lo[j])-real(xf,48+j*4));
        box.lo[k]=std::min(box.lo[k],x);box.hi[k]=std::max(box.hi[k],x);
    }
    return box;
}
static bool deadline(Snapshot& s){
    LARGE_INTEGER now;QueryPerformanceCounter(&now);
    if(uint64_t(now.QuadPart)-s.qpc<=uint64_t(frequency.QuadPart)/200)return false;
    s.budgetExceeded=true;return true;
}
// BOTW ContactLayer (botw-decomp physDefines.h EntityCollisionMask): layer is
// bits0-4, or bits25-29 in ground-hit form (bit31); bit30 marks ragdolls.
static uint32_t layerOf(uint32_t filter){return filter>>31?(filter>>25)&31:filter&31;}
// SkyCraft Collision.cpp Included(): static/anim-static/props/trees/terrain/
// ground/invisible walls. Mapped to EntityObject, GroundObject, AirWall,
// Ground/Smooth/Rough, Tree and WallForClimb. Player, NPC (SkyCraft excludes
// character controllers and bipeds), ragdoll, water, small objects, ropes and
// query-only layers are excluded, so neither Link nor his carried proxies collide.
// BOTW EntityGroupFilter (decomp physEntityGroupFilter.cpp): a ground-hit
// mask body collides with an entity only when its ground_hit_types (bits
// 8..23) do NOT contain that entity's GroundHit. Link is GroundHit::Player (0);
// route-07..15's 0x90FFFD00 shrine-cave box ignores everything but Animal.
constexpr uint32_t kGroundHitPlayer=0;
static bool includedLayer(uint32_t filter){
    if(filter>>31&&(filter>>8&0xFFFF)&(1u<<kGroundHitPlayer))return false;
    if(filter>>30&1)return false;
    switch(layerOf(filter)){case 0:case 2:case 7:case 8:case 9:case 10:case 12:case 15:return true;default:return false;}
}
static bool inventoryIsland(Snapshot& s,uint32_t island,uint32_t group){
    auto fail=[&](uint32_t address,uint32_t reason){s.failureAddress=address;s.inventoryFailure=reason;return false;};
    Block header;if(!readBlock(header,island,0x58))return fail(island,1);
    uint32_t address=word(header,0x4C),count=word(header,0x50),capacity=word(header,0x54)&0x3fffffff;
    if(count>capacity||count>s.candidates.size()-s.candidateCount)return fail(island,2);
    ++s.islands[group];s.bodies[group]+=count;s.entityTotal+=count;
    for(uint32_t i=0;i<count;i++){
        if(deadline(s))return fail(island,3);
        Block pointer,entity;
        if(uint64_t(address)+i*4>UINT32_MAX||!readBlock(pointer,address+i*4,4))return fail(address,4);
        uint32_t p=word(pointer,0);
        for(uint32_t j=0;j<s.candidateCount;j++)if(s.candidates[j].body==p)return fail(p,5);
        if(!readBlock(entity,p,0x80)||word(entity,0)!=0x10409FA8||word(entity,8)!=s.world)return fail(p,6);
        Candidate c;c.body=p;c.island=island;c.group=group;c.shape=word(entity,0x10);
        // hkpCollidable::m_broadPhaseHandle.m_collisionFilterInfo, entity+2C
        // (collidable +10, typed handle +14, filter +1C; layer-filter-21).
        c.filter=word(entity,0x2C);c.included=includedLayer(c.filter);
        // World+24 is Havok's shapeless fixed anchor, present in m_entities.
        if(!group&&p==s.fixedBody&&!c.shape){++s.emptyFixed;continue;}
        if(!readBlock(c.shapeBytes,c.shape,0x100))return fail(p,7);
        if(!readBlock(c.transform,word(entity,0x18),64)||!rigidTransform(c.transform)){
            copy(s,p,0x80);copy(s,word(entity,0x18),64);return fail(p,8);
        }
        // A box in the entity form of the filter may be a wall of the bridge's, on whatever layer it was put.
        if(word(c.shapeBytes,0)==0x10400CA8||word(c.shapeBytes,0)==0x104033A8){   // a box, or the hull an AirWall's box is (report 56)
            float at[3]{real(c.transform,0x30),real(c.transform,0x34),real(c.transform,0x38)};
            if(walls::mine(at)){
                c.included=false;uint32_t wanted=walls::filter.load();
                if(wanted&&c.filter!=wanted){body::put32(p+0x2C,wanted);++walls::refiltered;}
            }
        }
        if(group){float at[3]{real(c.transform,0x30),real(c.transform,0x34),real(c.transform,0x38)};lookThrow::body(c.transform.address,at);}
        c.vtable=word(c.shapeBytes,0);s.candidates[s.candidateCount++]=c;
    }
    return true;
}
static bool inventory(Snapshot& s,const Block& world){
    // US208 world destructor4097408,4097E58..4098054 enumerates these arrays;
    // island entity pointer/count are +4C/+50. No page is treated as a volume.
    lookThrow::stored();
    s.fixedBody=word(world,0x24);
    if(!inventoryIsland(s,word(world,0x20),0))return false;
    for(uint32_t group=1;group<=2;group++){
        uint32_t offset=group==1?0x28:0x34,address=word(world,offset),count=word(world,offset+4);
        if(count>(word(world,offset+8)&0x3fffffff)||count>256){s.inventoryFailure=9;s.failureAddress=address;return false;}
        for(uint32_t i=0;i<count;i++){
            Block pointer;if(uint64_t(address)+i*4>UINT32_MAX||!readBlock(pointer,address+i*4,4)||
                !inventoryIsland(s,word(pointer,0),group))return false;
        }
    }
    // What the game keeps from hitting its own player is not in Steve's way either. A filter of the regular form has
    // its group handler in bits 16 to 25 (physDefines.h EntityCollisionMask), and bodies of one group do not hit each
    // other: the player's body (layer 3) and what he carries. Observed (route-242, bombs pushed Steve
    // backwards bit by bit): a bomb over his head is a body on layer 0 with his body's group, half a metre
    // across at the height of Steve's head, and Steve was pushed 3.7 blocks in 6 s.
    uint32_t own=0,bodies=0;
    for(uint32_t i=0;i<s.candidateCount;i++){uint32_t f=s.candidates[i].filter;if(!(f>>31)&&(f&31)==3)own=(f>>16)&0x3FF;}
    if(own)for(uint32_t i=0;i<s.candidateCount;i++){auto& c=s.candidates[i];if(c.included&&!(c.filter>>31)&&((c.filter>>16)&0x3FF)==own){c.included=false;++bodies;}}
    {static uint32_t told=0;if(bodies!=told){told=bodies;event("{\"event\":\"native_carried\",\"group\":%u,\"bodies\":%u}",own,bodies);}}
    return true;
}
static uint32_t aabbFunction(uint32_t vt){
    // Pinned native getAabb implementations, shape-vtables16/shape-aabbs17.
    switch(vt){
        case 0x1040DA74:return 0x041336EC;
        case 0x10402F18:return 0x04023CFC;
        case 0x104033A8:return 0x0402CD00;
        case 0x10400CA8:return 0x03FFFBFC;
        case 0x10403DDC:return 0x04035A40;
        case 0x102CB778:return 0x0403D848;
        case 0x102CBAF8:return 0x034B11BC;
        case 0x1040D2E8:return 0x040F3614;
        case 0x1032214C:case 0x10322224:return 0x03815D94;
        default:return 0;
    }
}
// Pinned US208 RPX section bounds (rpx_dis.py): .text and the vtable data.
constexpr uint32_t kTextLo=0x02000020,kTextHi=0x04347C2C,kVtableLo=0x10000000,kVtableHi=0x10462BBC;
// Guarded virtual hkpShape::getAabbImpl: vtable entry 13 (+0x34) with its
// this-adjustment (+0x30) zero, inside the pinned code range, and equal to
// the previously pinned target whenever one is known (shape-vtables-16).
static uint32_t virtualAabb(uint32_t vt){
    if(vt<kVtableLo||uint64_t(vt)+0x38>kVtableHi||(vt&3)||!body::readable(vt,0x38))return 0;
    uint32_t adjust=body::u32(vt+0x30),fn=body::u32(vt+0x34);
    if(adjust||fn<kTextLo||fn>=kTextHi||(fn&3))return 0;
    auto pinned=aabbFunction(vt);
    return pinned&&pinned!=fn?0:fn;
}
// Exact pinned root-form bv-tree pairs; the container thunks subtract 0x14.
struct TreeCalls {uint32_t query=0,child=0;};
static TreeCalls treeCalls(uint32_t vt){
    if(vt==0x1040DA74)return {0x041353AC,0x04131B6C}; // hkpStaticCompoundShape
    if(vt==0x1040D2E8)return {0x040F3EC8,0x040EF924}; // hkpBvCompressedMeshShape
    return {};
}
// Static reuse. Everything that determines a fixed root's triangles: body,
// shape, filter, transform, the shape header (without the reference count at
// +4) and, for hkpStaticCompoundShape, its instance array. US208 layout from
// the route-18/19 shape dumps: +20 instance data, +24 count, +28 capacity,
// 0x40 bytes per instance (hkQsTransform, shape, filter, mask, user data);
// translation.w / scale.w hold the enable and disabled-small-key bits and
// header +38/+3C the disabled-large-key table. Group 0 is the fixed island.
// Zero means no signature: the root is read every time.
constexpr uint32_t kMaxInstances=8192;
static uint64_t mix(uint64_t h,const uint8_t* p,size_t n){
    for(size_t i=0;i+8<=n;i+=8){uint64_t w;memcpy(&w,p+i,8);h=(h^w)*0x9E3779B97F4A7C15ull;h^=h>>29;}
    return h;
}
static uint64_t rootSignature(const Candidate& c){
    if(c.group||c.shapeBytes.size<0x100||c.transform.size<64)return 0;
    uint64_t ids[]{uint64_t(c.body)<<32|c.shape,uint64_t(c.vtable)<<32|c.filter};
    uint64_t h=mix(0x534B5943,reinterpret_cast<const uint8_t*>(ids),sizeof(ids));
    h=mix(h,c.transform.bytes.data(),64);
    uint8_t header[0x100];memcpy(header,c.shapeBytes.bytes.data(),sizeof(header));memset(header+4,0,4);
    h=mix(h,header,sizeof(header));
    if(c.vtable==0x1040DA74){
        uint32_t data=word(c.shapeBytes,0x20),count=word(c.shapeBytes,0x24),capacity=word(c.shapeBytes,0x28)&0x3fffffff;
        if(count>capacity||count>kMaxInstances||data<0x10000000||(data&3)||uint64_t(data)+uint64_t(count)*0x40>0xA0000000)return 0;
        static uint8_t scratch[0x4000]; // one harvest at a time (state==1)
        for(uint32_t offset=0,total=count*0x40;offset<total;offset+=sizeof(scratch)){
            uint32_t n=std::min<uint32_t>(sizeof(scratch),total-offset);SIZE_T copied=0;
            if(!ReadProcessMemory(GetCurrentProcess(),body::base+data+offset,scratch,n,&copied)||copied!=n)return 0;
            h=mix(h,scratch,n);
        }
    }
    return h?h:1;
}
// Havok 2012 hkcdShapeType values with convex extent (sphere, cylinder, box,
// capsule, convex vertices, convex translate/transform) and shape lists.
static bool fallbackType(uint8_t type){
    switch(type){case 0:case 1:case 3:case 4:case 5:case 8:case 10:case 11:return true;default:return false;}
}
// SkyCraft Collision.cpp EmitAabbFallback() size limit: 64 MC blocks per axis.
static bool fallbackEligible(const Candidate& c){
    for(int k=0;k<3;k++)if((c.hi[k]-c.lo[k])/nativeRegions::scale>64)return false;
    return true;
}
static void classify(Snapshot& s,Candidate& c){
    uint8_t type=c.shapeBytes.bytes[8];
    if(treeCalls(c.vtable).query){c.kind=c.vtable==0x1040D2E8?Kind::Mesh:Kind::Compound;++s.meshRoots;}
    else if(c.vtable==0x1032214C||c.vtable==0x10322224)c.kind=Kind::Height;
    else if(fallbackType(type)&&fallbackEligible(c)){c.kind=Kind::Fallback;++s.fallbacks;}
    else{c.kind=Kind::Unsupported;++s.unsupported;}
}
// ---- Convex pieces (the harvest's side) -------------------------------------------------------------------
// Havok 2012 shapes as US v208 holds them (32-bit, big-endian), read from the guest during the world lock:
//   every shape: vtable +0, type +8, convex radius +10.
//   box (type 3, vtable 10400CA8): half extents +20 (route-07).
//   hull of points (type 5): bounds' half extents +20 and centre +30; the points +40 (pointer, count of groups,
//     capacity), each group 48 bytes: four x, four y, four z; the number of points +4C.
//   list (type 8, vtable 10403DDC): children +18 (pointer, count, capacity), 16 bytes each, the child's shape
//     first; bounds' half extents +30 and centre +40; a bit for each enabled child +50 (route-195, the shrine).
//   capsule (type 4, vtable 10401B14): its two ends +20 and +30, the radius +10 (route-198's dump).
//   cylinder (type 1, vtable 10403758): its two ends +20 and +30 (their w, +2C, the radius with the convex
//     radius added), two unit vectors across it +40 and +50.
//   sphere (type 0): the radius +10, about the shape's origin.
//   A round piece is given as the points of a twelve-sided one with its corners on the circle.
//   under a translation (type 10): child +18, translation +20.
//   under a transform (type 11): child +18, translation +20, rotation (x y z w) +30, scale +40.
// Nothing is taken on trust: a hull's points must fill the bounds the shape itself holds, a list's pieces the
// list's, and the whole must lie within the bounds the game's own getAabb gave. Anything else stays the box.
struct Affine {float c[3][3]{{1,0,0},{0,1,0},{0,0,1}},t[3]{};};   // world = t + sum of c[j]*local[j]
static void apply(const Affine& a,const float* in,float* out){for(int k=0;k<3;k++)out[k]=a.t[k]+a.c[0][k]*in[0]+a.c[1][k]*in[1]+a.c[2][k]*in[2];}
static Affine compose(const Affine& parent,const Affine& child){
    Affine r;apply(parent,child.t,r.t);
    for(int j=0;j<3;j++)for(int k=0;k<3;k++)r.c[j][k]=parent.c[0][k]*child.c[j][0]+parent.c[1][k]*child.c[j][1]+parent.c[2][k]*child.c[j][2];
    return r;
}
static Affine bodyAffine(const Block& xf){Affine a;for(int j=0;j<3;j++)for(int k=0;k<3;k++)a.c[j][k]=real(xf,j*16+k*4);for(int k=0;k<3;k++)a.t[k]=real(xf,48+k*4);return a;}
static bool guestBytes(uint32_t address,void* out,uint32_t size,uint32_t minimum=0x10000000){
    if(!body::base||!size||address<minimum||(address&3)||uint64_t(address)+size>0xA0000000)return false;
    SIZE_T copied=0;return ReadProcessMemory(GetCurrentProcess(),body::base+address,out,size,&copied)&&copied==size;
}
static uint32_t be32(const uint8_t* p){uint32_t v;memcpy(&v,p,4);return __builtin_bswap32(v);}
static float bef(const uint8_t* p){uint32_t v=be32(p);float f;memcpy(&f,&v,4);return f;}
static uint8_t pieceFail(Snapshot& s,uint32_t bodyAddress,uint32_t shape,const uint8_t* header,uint8_t reason){
    ++s.pieceFailures;
    if(s.pieceFailureCount<s.pieceFailed.size()){auto& f=s.pieceFailed[s.pieceFailureCount++];f.body=bodyAddress;f.shape=shape;f.reason=reason;if(header){f.vtable=be32(header);f.type=header[8];}
        if(s.pieceFailureCount<=6)copy(s,shape,0x80,0x10000);}
    return reason;
}
// The pieces of one shape, in the frame of the shape the body holds (`local` leads there). Returns 0 or the
// failure's reason; the caller takes back what was added.
static uint8_t shapePieces(Snapshot& s,uint32_t bodyAddress,uint32_t shape,const Affine& local,bool identity,int depth,uint32_t minimum=0x10000000){
    uint8_t h[0x70];
    if(depth>3)return pieceFail(s,bodyAddress,shape,nullptr,6);
    if(deadline(s))return pieceFail(s,bodyAddress,shape,nullptr,9);
    if(!guestBytes(shape,h,sizeof(h),minimum))return pieceFail(s,bodyAddress,shape,nullptr,1);
    const uint32_t vt=be32(h);const uint8_t type=h[8];const float radius=bef(h+0x10);
    auto add=[&](const float* points,uint32_t n)->bool{
        if(s.pieceCount>=s.pieces.size()||s.piecePointCount+n>s.piecePoints.size()/3)return false;
        Piece piece;piece.body=bodyAddress;piece.first=s.piecePointCount;piece.count=n;
        for(uint32_t i=0;i<n;i++){apply(local,points+i*3,s.piecePoints.data()+(s.piecePointCount+i)*3);for(int k=0;k<3;k++)if(!std::isfinite(s.piecePoints[(s.piecePointCount+i)*3+k]))return false;}
        s.piecePointCount+=n;s.pieces[s.pieceCount++]=piece;return true;
    };
    if(type==3&&vt==0x10400CA8){
        if(!std::isfinite(radius)||radius<0||radius>10)return pieceFail(s,bodyAddress,shape,h,1);
        float half[3],points[24];for(int k=0;k<3;k++){half[k]=bef(h+0x20+k*4)+radius;if(!std::isfinite(half[k])||half[k]<=0||half[k]>1000)return pieceFail(s,bodyAddress,shape,h,1);}
        for(int i=0;i<8;i++){points[i*3]=i&1?half[0]:-half[0];points[i*3+1]=i&2?half[1]:-half[1];points[i*3+2]=i&4?half[2]:-half[2];}
        return add(points,8)?0:pieceFail(s,bodyAddress,shape,h,5);
    }
    if(type==5){
        const uint32_t data=be32(h+0x40),groups=be32(h+0x44),n=be32(h+0x4C);
        if(!std::isfinite(radius)||radius<0||radius>10||n<4||n>kPiecePoints||groups!=(n+3)/4)return pieceFail(s,bodyAddress,shape,h,1);
        static uint8_t raw[(kPiecePoints/4)*48];static float points[kPiecePoints*3];   // one harvest at a time
        if(!guestBytes(data,raw,groups*48))return pieceFail(s,bodyAddress,shape,h,1);
        float lo[3]{1e30f,1e30f,1e30f},hi[3]{-1e30f,-1e30f,-1e30f};
        for(uint32_t i=0;i<n;i++)for(int k=0;k<3;k++){
            float v=bef(raw+(i/4)*48+k*16+(i%4)*4);if(!std::isfinite(v)||std::abs(v)>1e5f)return pieceFail(s,bodyAddress,shape,h,1);
            points[i*3+k]=v;lo[k]=std::min(lo[k],v);hi[k]=std::max(hi[k],v);
        }
        // The shape's own bounds: its points reach them on every side, the convex radius and a little apart.
        for(int k=0;k<3;k++){
            const float half=bef(h+0x20+k*4),centre=bef(h+0x30+k*4),slack=radius+.02f+2e-3f*std::abs(half);
            if(!std::isfinite(half)||!std::isfinite(centre)||std::abs(lo[k]-(centre-half))>slack||std::abs(hi[k]-(centre+half))>slack)return pieceFail(s,bodyAddress,shape,h,3);
        }
        return add(points,n)?0:pieceFail(s,bodyAddress,shape,h,5);
    }
    if((type==4&&vt==0x10401B14)||(type==1&&vt==0x10403758)||type==0){
        // Round pieces: rings of twelve points on the circle about the axis.
        constexpr int kSides=12;const float pi=3.14159265f,wide=1;
        float a[3]{},b[3]{},axis[3]{0,1,0},u[3],w[3],points[(4*kSides+2)*3];uint32_t n=0;float r=radius;
        if(type!=0){for(int k=0;k<3;k++){a[k]=bef(h+0x20+k*4);b[k]=bef(h+0x30+k*4);axis[k]=b[k]-a[k];}}
        if(type==1)r=bef(h+0x2C);
        if(!std::isfinite(r)||r<=0||r>1000)return pieceFail(s,bodyAddress,shape,h,1);
        float length=std::sqrt(axis[0]*axis[0]+axis[1]*axis[1]+axis[2]*axis[2]);
        if(!std::isfinite(length)||length>1e5f)return pieceFail(s,bodyAddress,shape,h,1);
        if(length<1e-5f){axis[0]=0;axis[1]=1;axis[2]=0;length=0;}else for(float& x:axis)x/=length;
        if(type==1){
            for(int k=0;k<3;k++){u[k]=bef(h+0x40+k*4);w[k]=bef(h+0x50+k*4);}
            const float lu=u[0]*u[0]+u[1]*u[1]+u[2]*u[2],lw=w[0]*w[0]+w[1]*w[1]+w[2]*w[2],across=u[0]*w[0]+u[1]*w[1]+u[2]*w[2];
            if(!(std::abs(lu-1)<1e-2f&&std::abs(lw-1)<1e-2f&&std::abs(across)<1e-2f))return pieceFail(s,bodyAddress,shape,h,1);
        }else{
            const float other[3]{std::abs(axis[0])<.9f?1.f:0.f,std::abs(axis[0])<.9f?0.f:1.f,0};
            u[0]=axis[1]*other[2]-axis[2]*other[1];u[1]=axis[2]*other[0]-axis[0]*other[2];u[2]=axis[0]*other[1]-axis[1]*other[0];
            const float lu=std::sqrt(u[0]*u[0]+u[1]*u[1]+u[2]*u[2]);for(float& x:u)x/=lu;
            w[0]=axis[1]*u[2]-axis[2]*u[1];w[1]=axis[2]*u[0]-axis[0]*u[2];w[2]=axis[0]*u[1]-axis[1]*u[0];
        }
        auto ring=[&](const float* centre,float along,float size){
            for(int i=0;i<kSides;i++){const float angle=2*pi*i/kSides,c=std::cos(angle)*size*wide,d=std::sin(angle)*size*wide;for(int k=0;k<3;k++)points[n*3+k]=centre[k]+axis[k]*along+u[k]*c+w[k]*d;++n;}
        };
        auto pole=[&](const float* centre,float along){for(int k=0;k<3;k++)points[n*3+k]=centre[k]+axis[k]*along;++n;};
        if(type==1){ring(a,0,r);ring(b,0,r);}   // a cylinder: flat ends
        else{   // a capsule or a sphere: a ring at each end's widest, one half way up each cap, the two poles
            const float half=.70710678f;
            ring(a,0,r);ring(a,-r*half,r*half);pole(a,-r);ring(b,0,r);ring(b,r*half,r*half);pole(b,r);
        }
        return add(points,n)?0:pieceFail(s,bodyAddress,shape,h,5);
    }
    if(type==8&&vt==0x10403DDC){
        const uint32_t data=be32(h+0x18),count=be32(h+0x1C);
        if(!count||count>256)return pieceFail(s,bodyAddress,shape,h,1);
        static uint8_t children[4][256*16];   // one for each depth
        if(!guestBytes(data,children[depth],count*16))return pieceFail(s,bodyAddress,shape,h,1);
        const uint32_t firstPoint=s.piecePointCount;
        for(uint32_t i=0;i<count;i++){
            if(!(be32(h+0x50+(i>>5)*4)&(1u<<(i&31))))continue;   // a child the game has switched off
            if(auto why=shapePieces(s,bodyAddress,be32(children[depth]+i*16),local,identity,depth+1))return why;
        }
        if(identity){
            // The list's own bounds, as for a hull. The pieces' convex radius is at most a tenth of a metre.
            float lo[3]{1e30f,1e30f,1e30f},hi[3]{-1e30f,-1e30f,-1e30f};
            for(uint32_t i=firstPoint;i<s.piecePointCount;i++)for(int k=0;k<3;k++){lo[k]=std::min(lo[k],s.piecePoints[i*3+k]);hi[k]=std::max(hi[k],s.piecePoints[i*3+k]);}
            if(s.piecePointCount==firstPoint)return pieceFail(s,bodyAddress,shape,h,4);
            for(int k=0;k<3;k++){
                const float half=bef(h+0x30+k*4),centre=bef(h+0x40+k*4),slack=.12f+2e-3f*std::abs(half);
                if(!std::isfinite(half)||!std::isfinite(centre)||std::abs(lo[k]-(centre-half))>slack||std::abs(hi[k]-(centre+half))>slack)return pieceFail(s,bodyAddress,shape,h,4);
            }
        }
        return 0;
    }
    if(type==10||type==11){
        Affine child;for(int k=0;k<3;k++){child.t[k]=bef(h+0x20+k*4);if(!std::isfinite(child.t[k])||std::abs(child.t[k])>1e5f)return pieceFail(s,bodyAddress,shape,h,7);}
        if(type==11){
            const float x=bef(h+0x30),y=bef(h+0x34),z=bef(h+0x38),w=bef(h+0x3C);float scale[3];
            for(int k=0;k<3;k++){scale[k]=bef(h+0x40+k*4);if(!std::isfinite(scale[k])||scale[k]<=0||scale[k]>1000)return pieceFail(s,bodyAddress,shape,h,7);}
            if(!(std::abs(x*x+y*y+z*z+w*w-1)<1e-3f))return pieceFail(s,bodyAddress,shape,h,7);
            const float m[3][3]{{1-2*(y*y+z*z),2*(x*y-z*w),2*(x*z+y*w)},{2*(x*y+z*w),1-2*(x*x+z*z),2*(y*z-x*w)},{2*(x*z-y*w),2*(y*z+x*w),1-2*(x*x+y*y)}};   // rows
            for(int j=0;j<3;j++)for(int k=0;k<3;k++)child.c[j][k]=m[k][j]*scale[j];
        }
        return shapePieces(s,bodyAddress,be32(h+0x18),compose(local,child),false,depth+1);
    }
    return pieceFail(s,bodyAddress,shape,h,2);
}
// The pieces of a body's shape (or of a compound's convex child, `minimum` low: it may lie on the stack) in the
// game's world, kept only when they lie within the bounds the game gave for it.
static bool worldPieces(Snapshot& s,uint32_t bodyAddress,uint32_t shape,const Block& xf,const float* lo,const float* hi,uint32_t minimum=0x10000000,uint32_t key=~0u){
    const uint32_t pieces=s.pieceCount,points=s.piecePointCount;const Affine world=bodyAffine(xf);
    auto back=[&]{s.pieceCount=pieces;s.piecePointCount=points;return false;};
    if(shapePieces(s,bodyAddress,shape,Affine{},true,0,minimum))return back();
    if(s.piecePointCount==points)return back();
    for(uint32_t i=points;i<s.piecePointCount;i++){
        float out[3];apply(world,s.piecePoints.data()+i*3,out);
        for(int k=0;k<3;k++){
            const float slack=.15f+.01f*(hi[k]-lo[k]);
            if(!std::isfinite(out[k])||out[k]<lo[k]-slack||out[k]>hi[k]+slack){pieceFail(s,bodyAddress,shape,nullptr,8);return back();}
            s.piecePoints[i*3+k]=out[k];
        }
    }
    for(uint32_t i=pieces;i<s.pieceCount;i++)s.pieces[i].key=key;
    return true;
}
static void terrainCandidate(Snapshot& s,const Block& shape,const Block& xf){
    if(s.terrain.shape||shape.size<0xC0||xf.size<64)return;
    auto vt=word(shape,0);if(vt!=0x1032214C&&vt!=0x10322224)return;
    // US208 getAabb03815D94 uses centre+A0, half extent+94, hkTransform.
    // This reader accepts only the observed translation-only terrain bodies.
    for(int j=0;j<16;j++)if(j!=12&&j!=14&&real(xf,j*4)!=(j==0||j==5||j==10?1.f:0.f))return;
    if(real(xf,48)!=real(shape,0x88)||real(xf,56)!=real(shape,0x90))return;
    float spacing=real(shape,0xB8);
    if(!std::isfinite(spacing)||spacing<=0||spacing>8||real(shape,0x40)!=spacing||real(shape,0x48)!=spacing||real(shape,0x44)!=1)return;
    for(int k=0;k<3;k++){
        float half=real(shape,0x94+k*4),center=real(shape,0xA0+k*4)+real(xf,48+k*4);
        if(!std::isfinite(half)||!std::isfinite(center)||half<0||s.queryLo[k]<center-half||s.queryHi[k]>center+half)return;
    }
    float offset[]{real(shape,0x88),real(shape,0x90)};
    for(float v:offset)if(!std::isfinite(v))return;
    TerrainGrid g;g.offset[0]=offset[0];g.offset[1]=offset[1];g.spacing=spacing;
    g.x=int(std::floor((s.queryLo[0]-offset[0])/spacing));g.z=int(std::floor((s.queryLo[2]-offset[1])/spacing));
    int lastX=int(std::ceil((s.queryHi[0]-offset[0])/spacing)),lastZ=int(std::ceil((s.queryHi[2]-offset[1])/spacing));
    if(g.x<0||g.z<0||lastX>=int(word(shape,0x28))||lastZ>=int(word(shape,0x2C)))return;
    g.width=lastX-g.x+1;g.depth=lastZ-g.z+1;
    if(g.width<2||g.depth<2||uint64_t(g.width)*g.depth>g.height.size())return;
    for(int k:{0,2}){
        float half=real(shape,0x94+k*4),center=real(shape,0xA0+k*4)+real(xf,48+k*4);
        int first=k==0?g.x:g.z,last=k==0?lastX:lastZ;float base=offset[k/2];
        if(base+first*spacing<center-half||base+last*spacing>center+half)return;
    }
    // A query touching the native exclusion circle stays unknown. Do not turn
    // the native getter's unavailable/excluded zero fallback into a floor.
    float radius2=real(shape,0xB4);
    if(!std::isfinite(radius2)||radius2<0)return;
    if(vt==0x1032214C&&radius2>0){
        float cx=real(shape,0xAC),cz=real(shape,0xB0);
        if(!std::isfinite(cx)||!std::isfinite(cz))return;
        float x=std::clamp(cx,offset[0]+g.x*spacing,offset[0]+lastX*spacing)-cx;
        float z=std::clamp(cz,offset[1]+g.z*spacing,offset[1]+lastZ*spacing)-cz;
        if(x*x+z*z<=radius2)return;
    }
    g.shape=shape.address;g.reader=word(shape,0x80);g.count=uint32_t(g.width*g.depth);s.terrain=g;
}
static void enter(CPU* c) {
    body::ret(c);
    c->gpr[3]=0;
    if(!machine(c,false)||harvestFrame)return;
    // r26 is the System singleton *slot*, r30 is ContactLayerType, r27 is
    // RayCast. Only our selected, epoch-valid diagnostic may request a copy.
    if(!calibration::begin || !calibration::running.id || lastId==calibration::running.id ||
        calibration::running.epoch!=body::lifecycleEpoch.load() || c->gpr[30]!=0 ||
        !body::readable(c->gpr[1]+0x200,0x400,0x10000))return;
    // RayCast's OnlyLockIfNeeded path can bypass the mutex on a physics worker.
    // The broader collector always requires its own nonblocking acquisition.
    if(!body::readable(c->gpr[26],4))return;
    auto system=body::u32(c->gpr[26]);if(!body::readable(system,0x20)||body::u32(system+0x14)!=2)return;
    auto table=body::u32(system+0x1C);if(!body::readable(table,8))return;
    auto wrapper=body::u32(table);if(!body::readable(wrapper,0x50))return;
    c->gpr[3]=wrapper+0x14; // 038DDFA4 -> 030BB668/670: native critical section.
}
static void locked(CPU* c) {
    body::ret(c);c->gpr[3]=0;
    if(harvestFrame)return;
    uint32_t expected=0;if(!state.compare_exchange_strong(expected,1))return;
    lastId=calibration::running.id;frameMarks::mark(frameMarks::kHarvest);
    harvestFrame=c->gpr[1];
    Snapshot& s=pending;
    s.id=lastId.load();s.epoch=calibration::running.epoch;s.layer=c->gpr[30];s.count=0;s.system=0;s.world=0;
    s.selected=0;s.keyCount=0;s.leaves=0;s.entityTotal=0;s.entityStart=0;s.ownedLockDepth=1;
    s.heightShape=0;s.heightReader=0;s.heightAvailable=-1;s.height=0;
    s.keyBlock=~0u;
    s.region.reset();auto region=nativeRegions::requested.load();
    if(region&&region->id==s.id&&region->stamp.body==s.epoch)s.region=*region;
    s.queries=s.saturatedQueries=s.triangleCount=s.unknownLeaves=s.maxKeys=s.keyFaults=s.keyFaultReason=s.overCapacity=0;leafSeen.fill(0);
    s.leafBoxCount=s.unknownCount=0;s.pieceCount=s.piecePointCount=s.pieceFailureCount=s.pieceFailures=0;
    s.compoundComplete=true;s.budgetExceeded=s.inventoryComplete=false;queryPending=0;s.reusedRoots=s.queriedRoots=s.doneRoots=s.decodedRoots=s.droppedKeys=0;s.continued=false;rootOpen=false;
    s.candidateCount=s.boundsRead=s.unsupported=0;boundsCursor=rootCursor=currentRoot=0;
    s.excluded=s.fallbacks=s.unboundable=s.meshRoots=s.heightfields=0;
    s.fixedBody=s.emptyFixed=s.inventoryFailure=s.failureAddress=0;
    std::fill_n(s.islands,3,0);std::fill_n(s.bodies,3,0);
    s.terrain={};
    if(!frequency.QuadPart)QueryPerformanceFrequency(&frequency);
    for(int i=0;i<3;i++) {
        s.queryLo[i]=calibration::running.from[i]-10.4f;
        s.queryHi[i]=calibration::running.from[i]+10.4f;
        if(s.region){
            s.queryLo[i]=nativeRegions::anchor[i]+(s.region->lo[i]-nativeRegions::origin[i])*nativeRegions::scale;
            s.queryHi[i]=nativeRegions::anchor[i]+(s.region->hi[i]-nativeRegions::origin[i])*nativeRegions::scale;
        }
        // Wrapper owns this aligned hkAabb for the duration of the native lock.
        body::putf(c->gpr[1]+0x200+i*4,s.queryLo[i]);
        body::putf(c->gpr[1]+0x210+i*4,s.queryHi[i]);
    }
    body::putf(c->gpr[1]+0x20C,0);body::putf(c->gpr[1]+0x21C,0);
    LARGE_INTEGER start;QueryPerformanceCounter(&start);s.qpc=start.QuadPart;
    if(copy(s,c->gpr[26],4)) {
        s.system=word(s.blocks[0],0);
        if(copy(s,s.system,0x40)) {
            uint32_t n=word(s.blocks[1],0x14),table=word(s.blocks[1],0x1C);
            if(n==2 && copy(s,table,8)) {
                uint32_t wrapper=word(s.blocks[2],0);
                for(const auto& lock:worldLocks)if(lock.wrapper==wrapper)s.ownedLockDepth+=lock.depth;
                if(copy(s,wrapper,0x40)) {
                    s.world=word(s.blocks[3],0);
                    if(copy(s,s.world,0x100)) {
                        s.inventoryComplete=inventory(s,s.blocks[4]);
                    }
                }
            }
        }
    }
    c->gpr[3]=0;
}
static void boundsNext(CPU* c){
    body::ret(c);c->gpr[3]=0;if(!harvesting(c))return;
    while(boundsCursor<pending.candidateCount){
        if(deadline(pending))return;
        auto& candidate=pending.candidates[boundsCursor];
        if(!candidate.included){candidate.kind=Kind::Excluded;++pending.excluded;++boundsCursor;continue;}
        auto fn=virtualAabb(candidate.vtable);
        // An included body without trustworthy bounds may overlap anything:
        // that is unavailable coverage, not an empty region.
        if(!fn){++pending.unboundable;++boundsCursor;continue;}
        candidate.aabbFn=fn;
        memcpy(body::base+c->gpr[1]+0x500,candidate.transform.bytes.data(),64);
        for(int i=0;i<8;i++)body::putf(c->gpr[1]+0x540+i*4,NAN);
        c->gpr[3]=candidate.shape;c->gpr[4]=c->gpr[1]+0x500;c->gpr[5]=c->gpr[1]+0x540;
        c->fpr[2]=0;c->gpr[12]=fn;return;
    }
}
static void boundsResult(CPU* c){
    body::ret(c);if(!harvesting(c)||boundsCursor>=pending.candidateCount)return;
    auto& candidate=pending.candidates[boundsCursor++];bool valid=true,overlap=true;
    for(int k=0;k<3;k++){
        candidate.lo[k]=body::f32(c->gpr[1]+0x540+k*4);candidate.hi[k]=body::f32(c->gpr[1]+0x550+k*4);
        valid&=std::isfinite(candidate.lo[k])&&std::isfinite(candidate.hi[k])&&candidate.lo[k]<=candidate.hi[k];
        overlap&=candidate.lo[k]<=pending.queryHi[k]&&candidate.hi[k]>=pending.queryLo[k];
    }
    candidate.bounded=valid;candidate.overlap=valid&&overlap;
    if(!valid){++pending.unboundable;return;}
    ++pending.boundsRead;if(!overlap)return;
    classify(pending,candidate);
    // One exact box keeps its own path (orientedBox); every other body of pieces is read piece by piece.
    if(candidate.kind==Kind::Fallback&&convexPieces.load()&&!(candidate.vtable==0x10400CA8&&candidate.shapeBytes.bytes[8]==3))
        candidate.pieces=worldPieces(pending,candidate.body,candidate.shape,candidate.transform,candidate.lo,candidate.hi);
    // Keep raw examples only for intersecting bodies. Inventory bytes are owned
    // separately and do not consume the old diagnostic block budget.
    copy(pending,candidate.body,0x80);copy(pending,candidate.shape,0x100);copy(pending,candidate.transform.address,64);
    if(candidate.kind==Kind::Height){
        ++pending.heightfields;
        pending.heightShape=candidate.shape;pending.heightReader=word(candidate.shapeBytes,0x80);
        if(pending.terrain.shape){candidate.kind=Kind::Unsupported;++pending.unsupported;return;}
        terrainCandidate(pending,candidate.shapeBytes,candidate.transform);
        if(!pending.terrain.shape){candidate.kind=Kind::Unsupported;++pending.unsupported;}
    }
}
static void nextQuery(CPU* c){
    body::ret(c);c->gpr[3]=0;if(!harvesting(c))return;
    // Nothing went wrong so far: every triangle of the roots read is owned.
    const bool sound=pending.compoundComplete&&!pending.unknownLeaves&&!pending.keyFaults&&pending.triangleCount<pending.triangles.size();
    if(!queryPending&&rootOpen){
        rootOpen=false;auto& root=pending.candidates[currentRoot];
        if(sound){root.done=true;++pending.doneRoots;}
    }
    if(pending.queries>=256||deadline(pending)){
        // A whole-region harvest stops between two queries of a signed root and
        // keeps the rest for the next frame; roots already done are kept too.
        const bool whole=pending.region&&pending.region->part==nativeRegions::kWhole;
        if(queryPending&&rootOpen&&whole&&sound&&pending.candidates[currentRoot].signature){
            auto& root=pending.candidates[currentRoot];
            continuation.generation=pending.region->stamp.generation;continuation.key=pending.region->key;
            continuation.body=root.body;continuation.signature=root.signature;continuation.chunk=root.chunk+1;
            continuation.count=queryPending;std::copy_n(queryStack.begin(),queryPending,continuation.stack.begin());
            root.partial=true;pending.continued=true;
        }else if(!queryPending&&whole&&sound&&pending.doneRoots)pending.continued=true;
        rootOpen=false;pending.budgetExceeded=true;pending.compoundComplete=false;queryPending=0;return;
    }
    while(!queryPending&&rootCursor<pending.candidateCount){
        auto index=rootCursor++;auto& candidate=pending.candidates[index];
        if((candidate.kind!=Kind::Compound&&candidate.kind!=Kind::Mesh)||!candidate.overlap)continue;
        candidate.signature=rootSignature(candidate);
        if(candidate.signature&&pending.region)for(uint32_t i=0;i<pending.region->knownCount&&!candidate.reused;i++)
            candidate.reused=pending.region->known[i].body==candidate.body&&pending.region->known[i].signature==candidate.signature;
        if(candidate.reused){++pending.reusedRoots;continue;}
        if(candidate.signature&&pending.region)for(uint32_t i=0;i<pending.region->decodedCount&&!candidate.decoded;i++)
            candidate.decoded=pending.region->decoded[i].body==candidate.body&&pending.region->decoded[i].signature==candidate.signature;
        if(candidate.decoded){
            ++pending.decodedRoots;
            // A compressed mesh root is decoded entirely by the worker.
            if(candidate.kind==Kind::Mesh){candidate.done=true;continue;}
        }
        ++pending.queriedRoots;currentRoot=index;pending.selected=candidate.shape;rootOpen=true;
        const auto* r=pending.region?&*pending.region:nullptr;
        if(r&&r->part==nativeRegions::kWhole&&candidate.signature&&r->resume.body==candidate.body&&r->resume.signature==candidate.signature&&
            continuation.count&&continuation.generation==r->stamp.generation&&continuation.key==r->key&&continuation.body==candidate.body&&
            continuation.signature==candidate.signature&&continuation.chunk==r->resumeChunk){
            // Resume exactly where the previous harvest of this root stopped.
            candidate.chunk=continuation.chunk;queryPending=continuation.count;
            std::copy_n(continuation.stack.begin(),queryPending,queryStack.begin());continuation.count=0;
        }else queryStack[queryPending++]=localQuery(candidate.transform,pending.queryLo,pending.queryHi);
    }
    if(!queryPending)return;
    auto calls=treeCalls(pending.candidates[currentRoot].vtable);
    if(!calls.query){pending.compoundComplete=false;queryPending=0;return;}
    c->gpr[27]=calls.query;c->gpr[26]=calls.child;
    keyBuffer=c->gpr[4];
    if(!keyBufferValid(keyBuffer)){++pending.keyFaults;pending.compoundComplete=false;queryPending=0;return;}
    for(uint32_t i=0;i<kKeyCanaries;i++)body::put32(keyBuffer+(kKeyCapacity+i)*4,canary(i));
    queryBox=queryStack[--queryPending];++pending.queries;
    for(int k=0;k<3;k++){
        body::putf(c->gpr[1]+0x200+k*4,queryBox.lo[k]);body::putf(c->gpr[1]+0x210+k*4,queryBox.hi[k]);
    }
    c->gpr[3]=pending.selected;
}
static void keys(CPU* c) {
    body::ret(c);if(!harvesting(c))return;
    pending.keyCount=c->gpr[3];
    // A count above capacity with intact canaries is a total-hit count (keys
    // past capacity were not written; session-12): saturation, not a fault.
    uint32_t reason=c->gpr[4]!=keyBuffer||!keyBufferValid(keyBuffer)?1:0;
    for(uint32_t i=0;!reason&&i<kKeyCanaries;i++)if(body::u32(keyBuffer+(kKeyCapacity+i)*4)!=canary(i))reason=3;
    if(!reason&&pending.keyCount>kKeyCapacity){++pending.overCapacity;pending.keyCount=kKeyCapacity;}
    if(reason){
        pending.keyFaultReason=reason;
        // Never consume keys from a buffer that may have been overrun.
        ++pending.keyFaults;pending.compoundComplete=false;c->gpr[3]=0;return;
    }
    pending.maxKeys=std::max(pending.maxKeys,pending.keyCount);
    if(pending.keyCount>=kKeyCapacity){
        ++pending.saturatedQueries;c->gpr[3]=0;
        int axis=0;for(int k=1;k<3;k++)if(queryBox.hi[k]-queryBox.lo[k]>queryBox.hi[axis]-queryBox.lo[axis])axis=k;
        float mid=(queryBox.lo[axis]+queryBox.hi[axis])*.5f;
        if(queryBox.depth>=12||queryPending+2>queryStack.size()||mid<=queryBox.lo[axis]||mid>=queryBox.hi[axis]){
            pending.compoundComplete=false;return;
        }
        QueryBox a=queryBox,b=queryBox;a.hi[axis]=mid;b.lo[axis]=mid;++a.depth;++b.depth;
        queryStack[queryPending++]=b;queryStack[queryPending++]=a;return;
    }
    for(uint32_t i=0;i<pending.keyCount;i++)currentKeys[i]=body::u32(c->gpr[4]+i*4);
    if(const auto& root=pending.candidates[currentRoot];root.decoded){
        // Decoded static compound: keys of compressed-mesh instances are the
        // worker's. Only the other children (convex pieces) are fetched here,
        // so the key buffer is compacted in place before the guest's leaf loop.
        uint32_t bits=root.shapeBytes.bytes[0x18],data=word(root.shapeBytes,0x20),count=word(root.shapeBytes,0x24),kept=0,last=~0u;bool keep=false;
        for(uint32_t i=0;i<pending.keyCount;i++){
            uint32_t index=bits&&bits<32?currentKeys[i]>>bits:~0u;
            if(index!=last){
                if(index>=count||!body::readable(data+index*0x40,0x40)){++pending.keyFaults;pending.keyFaultReason=4;pending.compoundComplete=false;c->gpr[3]=0;return;}
                uint32_t child=body::u32(data+index*0x40+0x30);keep=!(body::readable(child,4)&&body::u32(child)==nativeMesh::kMesh);last=index;
            }
            if(keep){currentKeys[kept]=currentKeys[i];body::put32(c->gpr[4]+kept*4,currentKeys[i]);++kept;}
        }
        pending.droppedKeys+=pending.keyCount-kept;pending.keyCount=kept;c->gpr[3]=kept;
    }
    if(pending.keyCount>0 && pending.keyCount<=64) {
        uint32_t index=pending.count;
        if(copy(pending,c->gpr[4],pending.keyCount*4,0x10000))pending.keyBlock=index;
    }
}
static void unknownLeaf(CPU* c,uint32_t address,uint32_t key,uint8_t reason){
    ++pending.unknownLeaves;pending.compoundComplete=false;
    if(pending.unknownCount>=pending.unknown.size())return;
    auto& u=pending.unknown[pending.unknownCount++];u.body=pending.candidates[currentRoot].body;u.key=key;u.reason=reason;
    if(body::readable(address,0x10,0x10000)){u.vtable=body::u32(address);u.type=body::base[address+8];}
    uint32_t index=pending.count;if(pending.unknownCount<=4&&copy(pending,address,256,0x10000))u.block=index;
}
static uint32_t leafPendingKey=0,leafShape=0;
static void leaf(CPU* c) {
    body::ret(c);uint32_t address=c->gpr[3];c->gpr[3]=0;if(!harvesting(c))return;
    auto indexInQuery=c->gpr[29];
    if(indexInQuery>=pending.keyCount||!body::readable(address,0x50,0x10000)){unknownLeaf(c,address,~0u,1);return;}
    uint32_t key=currentKeys[indexInQuery];
    if(!leafFirst(pending.candidates[currentRoot].body,key))return;
    if(body::u32(address)!=0x1040677C||body::base[address+8]!=2){
        // Eligible convex child: request its own guarded getAabb in the root
        // body's transform (asm calls r12 with r3 child, r4 xf, r5 out).
        auto fn=virtualAabb(body::u32(address));
        if(!fallbackType(body::base[address+8])||!fn||pending.leafBoxCount>=pending.leafBoxes.size()){unknownLeaf(c,address,key,2);return;}
        const auto& root=pending.candidates[currentRoot];
        memcpy(body::base+c->gpr[1]+0x500,root.transform.bytes.data(),64);
        for(int i=0;i<8;i++)body::putf(c->gpr[1]+0x540+i*4,NAN);
        leafPendingKey=key;leafShape=address;
        c->gpr[3]=address;c->gpr[4]=c->gpr[1]+0x500;c->gpr[5]=c->gpr[1]+0x540;c->fpr[2]=0;c->gpr[12]=fn;
        return;
    }
    if(pending.triangleCount==pending.triangles.size()){unknownLeaf(c,address,key,3);return;}
    const auto& root=pending.candidates[currentRoot];
    Triangle tri;tri.body=root.body;tri.key=key;tri.radius=body::f32(address+0x10);
    bool valid=std::isfinite(tri.radius)&&tri.radius>=0;
    for(int v=0;v<3;v++)for(int k=0;k<3;k++){
        float x=real(root.transform,48+k*4);
        for(int j=0;j<3;j++)x+=real(root.transform,j*16+k*4)*body::f32(address+0x20+v*16+j*4);
        tri.v[v*3+k]=x;valid&=std::isfinite(x);
    }
    if(!valid){unknownLeaf(c,address,key,4);return;}
    pending.triangles[pending.triangleCount++]=tri;
    if(pending.leaves>=4)return; // Retain bounded raw examples; all triangles above are owned.
    // Returned buffers/borrowed children are copied before releasing native lock.
    uint32_t index=pending.count;
    if(copy(pending,address,256,0x10000)) {
        pending.leafBlocks[pending.leaves]=index;
        pending.leafKeys[pending.leaves]=pending.keyBlock<pending.count && c->gpr[29]<pending.keyCount ?
            word(pending.blocks[pending.keyBlock],c->gpr[29]*4):~0u;
        ++pending.leaves;
    }
}
static void leafBounds(CPU* c){
    body::ret(c);if(!harvesting(c))return;
    uint32_t child=c->gpr[1]+0x300;LeafBox b;b.body=pending.candidates[currentRoot].body;b.key=leafPendingKey;
    if(body::readable(child,0x10,0x10000)){b.vtable=body::u32(child);b.type=body::base[child+8];}
    bool valid=true;
    for(int k=0;k<3;k++){
        b.lo[k]=body::f32(c->gpr[1]+0x540+k*4);b.hi[k]=body::f32(c->gpr[1]+0x550+k*4);
        valid&=std::isfinite(b.lo[k])&&std::isfinite(b.hi[k])&&b.lo[k]<=b.hi[k]&&(b.hi[k]-b.lo[k])/nativeRegions::scale<=64;
    }
    if(!valid){unknownLeaf(c,child,leafPendingKey,5);return;}
    // The child's pieces, within the bounds the game has just given for it; else the box of those bounds.
    const auto& root=pending.candidates[currentRoot];
    if(convexPieces.load()&&worldPieces(pending,root.body,leafShape,root.transform,b.lo,b.hi,0x10000,leafPendingKey))return;
    pending.leafBoxes[pending.leafBoxCount++]=b;
}
static void heightPrepare(CPU* c) {
    body::ret(c);c->gpr[3]=0;if(!harvesting(c) || !pending.heightReader)return;
    // Exact helper arguments from the US208 height getters (physics-leads-12).
    // This checks one reader's availability, not heightfield-cell coverage:
    // This legacy single point is diagnostic; the grid below has its own gates.
    if(!copy(pending,pending.heightReader,0xC0))return;
    body::putf(c->gpr[1]+0x260,calibration::running.from[0]);
    body::putf(c->gpr[1]+0x264,calibration::running.from[2]);
    body::putf(c->gpr[1]+0x268,0);
    c->gpr[3]=pending.heightReader;
}
static void heightResult(CPU* c) {
    body::ret(c);if(!harvesting(c))return;
    pending.heightAvailable=c->gpr[3]!=0;
    if(pending.heightAvailable)pending.height=body::f32(c->gpr[1]+0x268);
}
static void terrainNext(CPU* c){
    body::ret(c);c->gpr[3]=0;if(!harvesting(c))return;
    auto& g=pending.terrain;if(!g.shape||g.read>=g.count)return;
    LARGE_INTEGER now;QueryPerformanceCounter(&now);
    if(uint64_t(now.QuadPart)-pending.qpc>uint64_t(frequency.QuadPart)/200){pending.budgetExceeded=true;return;}
    float x=g.offset[0]+(g.x+int(g.read%g.width))*g.spacing;
    float z=g.offset[1]+(g.z+int(g.read/g.width))*g.spacing;
    body::putf(c->gpr[1]+0x260,x);body::putf(c->gpr[1]+0x264,z);body::putf(c->gpr[1]+0x268,0);
    c->gpr[3]=g.reader;
}
static void terrainResult(CPU* c){
    body::ret(c);if(!harvesting(c))return;
    auto& g=pending.terrain;if(g.read>=g.count)return;
    float height=body::f32(c->gpr[1]+0x268);
    bool valid=c->gpr[3]!=0&&std::isfinite(height)&&std::abs(height)<100000;
    g.valid[g.read]=valid;g.height[g.read]=valid?height:0;++g.read;g.available+=valid;
    g.complete=g.read==g.count&&g.available==g.count;
}
static void terrainTriangles(const TerrainGrid& g,std::vector<botwcraft::proto::ColTri>& out){
    if(!g.complete)return;
    for(int z=0;z<g.depth-1;z++)for(int x=0;x<g.width-1;x++){
        // getTriangleFlip0381879C=false. Generic cast042FFF6C..04300038
        // uses the (x+1,z)--(x,z+1) diagonal and upward normals.
        float v[4][3];int i=0;
        for(int dz=0;dz<2;dz++)for(int dx=0;dx<2;dx++,i++){
            v[i][0]=g.offset[0]+(g.x+x+dx)*g.spacing;
            v[i][1]=g.height[(z+dz)*g.width+x+dx];
            v[i][2]=g.offset[1]+(g.z+z+dz)*g.spacing;
        }
        for(auto indices:std::array<std::array<int,3>,2>{{{0,2,1},{1,2,3}}}){
            botwcraft::proto::ColTri t{};t.flags=botwcraft::proto::kTriTerrain;
            for(int j=0;j<3;j++)for(int k=0;k<3;k++)t.v[j*3+k]=(v[indices[j]][k]-nativeRegions::anchor[k])/nativeRegions::scale+nativeRegions::origin[k];
            out.push_back(t);
        }
    }
}
static void finish(CPU* c) {
    body::ret(c);machine(c,true);
    if(!harvesting(c))return;
    LARGE_INTEGER end;QueryPerformanceCounter(&end);pending.ticks=end.QuadPart-pending.qpc;frameMarks::mark(frameMarks::kHarvestEnd);
    harvestFrame=0;state.store(2,std::memory_order_release);SetEvent(harvested);
}
// Loaded-terrain guard. Field terrain is 250x250 BvCompressedMesh ground
// tiles (session-09/11 inventories). An unloaded tile looks like empty air,
// so when tiles exist every XZ corner of the query must lie in one; a region
// is far smaller than a tile, so its corners touch every tile it overlaps.
// Stages without tiles (shrines, interiors) are judged by their bodies alone.
static bool groundTile(const Candidate& c){
    uint32_t layer=layerOf(c.filter);
    return c.vtable==0x1040D2E8&&c.bounded&&c.included&&layer>=8&&layer<=10&&c.hi[0]-c.lo[0]>=200&&c.hi[2]-c.lo[2]>=200;
}
static bool terrainLoaded(const Snapshot& s){
    bool tiles=false;
    for(uint32_t i=0;i<s.candidateCount;i++)tiles|=groundTile(s.candidates[i]);
    if(!tiles)return true;
    for(int corner=0;corner<4;corner++){
        float x=corner&1?s.queryHi[0]:s.queryLo[0],z=corner&2?s.queryHi[2]:s.queryLo[2];bool inside=false;
        for(uint32_t i=0;i<s.candidateCount&&!inside;i++){
            const auto& c=s.candidates[i];
            inside=groundTile(c)&&x>=c.lo[0]&&x<=c.hi[0]&&z>=c.lo[2]&&z<=c.hi[2];
        }
        if(!inside)return false;
    }
    return true;
}
// Every included body that can reach the region is read exactly or by an
// eligible AABB fallback, over loaded terrain; otherwise the region is unknown.
static bool regionComplete(const Snapshot& s){
    return s.region&&s.inventoryComplete&&!s.budgetExceeded&&s.compoundComplete&&!s.unknownLeaves&&
        !s.keyFaults&&!s.unsupported&&!s.unboundable&&s.triangleCount<s.triangles.size()&&
        (!s.heightfields||s.terrain.complete)&&terrainLoaded(s);
}
// SkyCraft EmitAabbFallback(): an axis-aligned box from native world bounds.
// Outward winding, so floors face +Y like every other exported triangle.
static void boxTriangles(const float* nativeLo,const float* nativeHi,std::vector<botwcraft::proto::ColTri>& out){
    float lo[3],hi[3];
    for(int k=0;k<3;k++){
        lo[k]=(nativeLo[k]-nativeRegions::anchor[k])/nativeRegions::scale+nativeRegions::origin[k];
        hi[k]=(nativeHi[k]-nativeRegions::anchor[k])/nativeRegions::scale+nativeRegions::origin[k];
    }
    for(int axis=0;axis<3;axis++)for(int side=0;side<2;side++){
        int u=(axis+1)%3,v=(axis+2)%3;float corner[4][3];
        for(int i=0;i<4;i++){
            corner[i][axis]=side?hi[axis]:lo[axis];
            corner[i][u]=(i==1||i==2)?hi[u]:lo[u];corner[i][v]=(i>=2)?hi[v]:lo[v];
        }
        for(auto tri:std::array<std::array<int,3>,2>{{{0,1,2},{0,2,3}}}){
            botwcraft::proto::ColTri t{};
            for(int j=0;j<3;j++)std::copy_n(corner[tri[j]],3,t.v+j*3);
            float a[3],b[3],n[3];nativeGeometry::sub(t.v+3,t.v,a);nativeGeometry::sub(t.v+6,t.v,b);nativeGeometry::cross(a,b,n);
            if((n[axis]>0)!=bool(side))for(int k=0;k<3;k++)std::swap(t.v[3+k],t.v[6+k]);
            out.push_back(t);
        }
    }
}
// The faces of the convex hull of a piece's points, as triangles wound outward (the game's world). Every plane
// through three points that has all the others on one side is a face; the points on it, in order around their
// middle, are its outline. Slow in the number of points and so kept by the points' hash: a body that does not
// move costs it once.
static bool hullTriangles(const float* points,uint32_t n,std::vector<std::array<float,9>>& out){
    if(n<4||n>kPiecePoints)return false;
    double lo[3]{1e30,1e30,1e30},hi[3]{-1e30,-1e30,-1e30};
    for(uint32_t i=0;i<n;i++)for(int k=0;k<3;k++){lo[k]=std::min(lo[k],double(points[i*3+k]));hi[k]=std::max(hi[k],double(points[i*3+k]));}
    // About the middle of the points: with the world's large numbers a plane's offset loses its last digits
    // (route-196: 14 points gave 208 triangles, each face found again from every three of its corners).
    const double centre[3]{(lo[0]+hi[0])/2,(lo[1]+hi[1])/2,(lo[2]+hi[2])/2};
    const double extent=std::max({hi[0]-lo[0],hi[1]-lo[1],hi[2]-lo[2]}),eps=std::max(1e-3,2e-4*extent);
    std::vector<std::array<double,3>> v;v.reserve(n);
    for(uint32_t i=0;i<n;i++){
        std::array<double,3> p{points[i*3]-centre[0],points[i*3+1]-centre[1],points[i*3+2]-centre[2]};bool twin=false;
        for(const auto& q:v)if(std::abs(q[0]-p[0])<eps&&std::abs(q[1]-p[1])<eps&&std::abs(q[2]-p[2])<eps){twin=true;break;}
        if(!twin)v.push_back(p);
    }
    const size_t m=v.size();if(m<4)return false;
    using Set=std::array<uint64_t,kPiecePoints/64>;   // the points of one face
    std::vector<Set> faces;std::vector<size_t> face;std::vector<std::array<double,3>> flat,ring;const size_t before=out.size();
    auto dot=[](const double* a,const double* b){return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];};
    auto has=[](const Set& s,size_t q){return bool(s[q>>6]>>(q&63)&1);};
    for(size_t i=0;i<m;i++)for(size_t j=i+1;j<m;j++)for(size_t k=j+1;k<m;k++){
        bool known=false;for(const auto& f:faces)if(has(f,i)&&has(f,j)&&has(f,k)){known=true;break;}   // three corners of a face already made
        if(known)continue;
        double a[3],b[3],normal[3];for(int x=0;x<3;x++){a[x]=v[j][x]-v[i][x];b[x]=v[k][x]-v[i][x];}
        normal[0]=a[1]*b[2]-a[2]*b[1];normal[1]=a[2]*b[0]-a[0]*b[2];normal[2]=a[0]*b[1]-a[1]*b[0];
        double length=std::sqrt(dot(normal,normal));if(length<1e-2*std::sqrt(dot(a,a)*dot(b,b))||length<1e-12)continue;   // three points nearly in a line
        for(double& x:normal)x/=length;double d=dot(normal,v[i].data());
        bool above=false,below=false;
        for(size_t q=0;q<m&&!(above&&below);q++){double side=dot(normal,v[q].data())-d;if(side>eps)above=true;else if(side<-eps)below=true;}
        if(above&&below)continue;
        if(!above&&!below){out.resize(before);return false;}   // every point in one plane: no solid
        if(above){for(double& x:normal)x=-x;d=-d;}
        double middle[3]{};face.clear();Set set{};
        for(size_t q=0;q<m;q++)if(std::abs(dot(normal,v[q].data())-d)<=eps){face.push_back(q);set[q>>6]|=1ull<<(q&63);for(int x=0;x<3;x++)middle[x]+=v[q][x];}
        faces.push_back(set);
        for(double& x:middle)x/=double(face.size());
        double u[3],w[3],best=-1;
        for(size_t q:face){double r[3];for(int x=0;x<3;x++)r[x]=v[q][x]-middle[x];double l=dot(r,r);if(l>best){best=l;for(int x=0;x<3;x++)u[x]=r[x];}}
        if(best<=0)continue;
        const double reach=std::sqrt(best);for(double& x:u)x/=reach;
        w[0]=normal[1]*u[2]-normal[2]*u[1];w[1]=normal[2]*u[0]-normal[0]*u[2];w[2]=normal[0]*u[1]-normal[1]*u[0];
        // The face's outline: the convex hull of its points in the plane (Andrew's chain). A point within the
        // face or along one of its edges is no corner and drops out.
        flat.clear();ring.clear();
        for(size_t q:face){double r[3];for(int x=0;x<3;x++)r[x]=v[q][x]-middle[x];flat.push_back({dot(r,u),dot(r,w),double(q)});}
        std::sort(flat.begin(),flat.end());
        auto turn=[](const std::array<double,3>& o,const std::array<double,3>& a,const std::array<double,3>& b){return (a[0]-o[0])*(b[1]-o[1])-(a[1]-o[1])*(b[0]-o[0]);};
        for(int pass=0;pass<2;pass++){
            const size_t first=ring.size();
            for(size_t q=0;q<flat.size();q++){
                const auto& point=pass?flat[flat.size()-1-q]:flat[q];
                while(ring.size()>=first+2&&turn(ring[ring.size()-2],ring.back(),point)<=eps*reach)ring.pop_back();
                ring.push_back(point);
            }
            ring.pop_back();
        }
        for(size_t f=1;f+1<ring.size();f++){
            const auto& p0=v[size_t(ring[0][2])];const auto& p1=v[size_t(ring[f][2])];const auto& p2=v[size_t(ring[f+1][2])];
            double e1[3],e2[3],cr[3];for(int x=0;x<3;x++){e1[x]=p1[x]-p0[x];e2[x]=p2[x]-p0[x];}
            cr[0]=e1[1]*e2[2]-e1[2]*e2[1];cr[1]=e1[2]*e2[0]-e1[0]*e2[2];cr[2]=e1[0]*e2[1]-e1[1]*e2[0];
            if(dot(cr,cr)<1e-12)continue;   // no area
            std::array<float,9> triangle;const bool outward=dot(cr,normal)>0;const auto& second=outward?p1:p2;const auto& third=outward?p2:p1;
            for(int x=0;x<3;x++){triangle[x]=float(p0[x]+centre[x]);triangle[3+x]=float(second[x]+centre[x]);triangle[6+x]=float(third[x]+centre[x]);}
            out.push_back(triangle);
        }
    }
    if(faces.size()<4){out.resize(before);return false;}
    return true;
}
static uint64_t pieceHulls=0,pieceHullsMade=0,pieceTriangles=0,pieceHullFailures=0;
// A piece's triangles in Minecraft's coordinates; false when its points make no solid (the caller boxes them).
static bool pieceTrianglesOf(const float* points,uint32_t n,std::vector<botwcraft::proto::ColTri>& out){
    static std::unordered_map<uint64_t,std::vector<std::array<float,9>>> kept;
    uint64_t h=1469598103934665603ULL^n;const auto* bytes=reinterpret_cast<const uint8_t*>(points);
    for(size_t i=0;i<size_t(n)*12;i++){h^=bytes[i];h*=1099511628211ULL;}
    auto found=kept.find(h);++pieceHulls;
    if(found==kept.end()){
        if(kept.size()>=2048)kept.clear();
        std::vector<std::array<float,9>> made;if(!hullTriangles(points,n,made))made.clear();
        ++pieceHullsMade;found=kept.emplace(h,std::move(made)).first;
        static uint64_t told=0;
        if(GetTickCount64()-told>=1000){told=GetTickCount64();event("{\"event\":\"native_pieces\",\"hulls_made\":%llu,\"hulls_asked\":%llu,\"no_solid\":%llu,\"triangles\":%llu,\"points\":%u,\"faces\":%zu}",pieceHullsMade,pieceHulls,pieceHullFailures,pieceTriangles,n,found->second.size());}
    }
    if(found->second.empty()){++pieceHullFailures;return false;}
    for(const auto& triangle:found->second){
        botwcraft::proto::ColTri t{};
        for(int j=0;j<9;j++)t.v[j]=(triangle[j]-nativeRegions::anchor[j%3])/nativeRegions::scale+nativeRegions::origin[j%3];
        if(nativeGeometry::valid(t)){out.push_back(t);++pieceTriangles;}
    }
    return true;
}
// Exact hkpBoxShape (US208 vtable 10400CA8, getAabb 03FFFBFC): the getAabb
// reads the convex radius at +0x10 and half extents at +0x20..+0x28. A
// rotated box's world AABB can be far larger than the box (route-07: a
// 141-degree box became a 28x23x26 MC wall), so pinned boxes are exported as
// their 12 oriented triangles. Anything else keeps SkyCraft's AABB fallback.
static bool orientedBox(const Candidate& c,std::vector<botwcraft::proto::ColTri>& out){
    if(c.vtable!=0x10400CA8||c.aabbFn!=0x03FFFBFC||c.shapeBytes.bytes[8]!=3||c.shapeBytes.size<0x2C||c.transform.size<64)return false;
    float radius=real(c.shapeBytes,0x10),half[3];
    for(int k=0;k<3;k++){half[k]=real(c.shapeBytes,0x20+k*4)+radius;if(!std::isfinite(half[k])||half[k]<=0||half[k]>1000)return false;}
    if(!std::isfinite(radius)||radius<0)return false;
    float corner[8][3],centre[3];
    for(int k=0;k<3;k++)centre[k]=(real(c.transform,48+k*4)-nativeRegions::anchor[k])/nativeRegions::scale+nativeRegions::origin[k];
    for(int i=0;i<8;i++){
        float local[3]{i&1?half[0]:-half[0],i&2?half[1]:-half[1],i&4?half[2]:-half[2]};
        for(int k=0;k<3;k++){
            float x=real(c.transform,48+k*4);
            for(int j=0;j<3;j++)x+=real(c.transform,j*16+k*4)*local[j];
            corner[i][k]=(x-nativeRegions::anchor[k])/nativeRegions::scale+nativeRegions::origin[k];
            if(!std::isfinite(corner[i][k]))return false;
        }
    }
    // Faces as corner quads (bit axis fixed), wound outward from the centre.
    static constexpr int faces[6][4]{{0,2,6,4},{1,3,7,5},{0,1,5,4},{2,3,7,6},{0,1,3,2},{4,5,7,6}};
    for(const auto& f:faces)for(auto tri:std::array<std::array<int,3>,2>{{{0,1,2},{0,2,3}}}){
        botwcraft::proto::ColTri t{};
        for(int j=0;j<3;j++)std::copy_n(corner[f[tri[j]]],3,t.v+j*3);
        float a[3],b[3],n[3],d[3];nativeGeometry::sub(t.v+3,t.v,a);nativeGeometry::sub(t.v+6,t.v,b);nativeGeometry::cross(a,b,n);
        nativeGeometry::sub(t.v,centre,d);
        if(nativeGeometry::dot(n,d)<0)for(int k=0;k<3;k++)std::swap(t.v[3+k],t.v[6+k]);
        out.push_back(t);
    }
    return true;
}
// Diagnostic for the direct mesh decoder: on a new token in mesh-dump.txt, the
// next harvest that read at least 200 triangles is saved twice: its usual
// JSON (native triangles with their shape keys) and the raw guest bytes of
// every queried root (compressed-mesh arrays, static-compound instances and
// their child shapes). Worker thread, read-only, bounded; evidence only.
static void dumpRange(std::ofstream& f,std::vector<std::pair<uint32_t,uint32_t>>& done,uint64_t& budget,uint32_t address,uint64_t size){
    if(!size||size>budget||address<0x10000000||(address&3)||uint64_t(address)+size>0xA0000000)return;
    for(const auto& d:done)if(d.first==address&&d.second>=size)return;
    // body::base is per guest thread; the worker asks Cemu for the mapping.
    auto* guestBase=static_cast<uint8_t*>(getGuest());std::vector<uint8_t> bytes(size);SIZE_T copied=0;
    if(!guestBase||!ReadProcessMemory(GetCurrentProcess(),guestBase+address,bytes.data(),size,&copied)||copied!=size)return;
    uint32_t header[]{address,uint32_t(size)};f.write(reinterpret_cast<const char*>(header),8);f.write(reinterpret_cast<const char*>(bytes.data()),size);
    done.push_back({address,uint32_t(size)});budget-=size;
}
static bool workerBlock(Block& b,uint32_t address,uint32_t size){
    auto* guestBase=static_cast<uint8_t*>(getGuest());SIZE_T copied=0;b.address=address;b.size=size;
    return guestBase&&size<=b.bytes.size()&&address>=0x10000000&&!(address&3)&&uint64_t(address)+size<=0xA0000000&&
        ReadProcessMemory(GetCurrentProcess(),guestBase+address,b.bytes.data(),size,&copied)&&copied==size;
}
static void dumpMesh(std::ofstream& f,std::vector<std::pair<uint32_t,uint32_t>>& done,uint64_t& budget,uint32_t shape){
    Block h;if(!workerBlock(h,shape,0x100))return;
    dumpRange(f,done,budget,shape,0x100);
    if(word(h,0)!=0x1040D2E8)return;
    // hkcdDefaultStaticMeshTree arrays (pointer, count) and element sizes.
    static constexpr std::pair<uint32_t,uint32_t> arrays[]{{0x20,4},{0x2C,4},{0x50,8},{0x8C,0x60},{0x98,4},{0xA4,2},{0xB0,4},{0xBC,8},{0xC8,4}};
    for(auto [offset,element]:arrays){uint64_t bytes=(uint64_t(word(h,offset+4))*element+3)&~3ull;dumpRange(f,done,budget,word(h,offset),bytes);}
}
static void dumpRoots(const Snapshot& s,const std::string& path){
    std::ofstream f(path,std::ios::binary);uint32_t magic=0x314D4B48;f.write(reinterpret_cast<const char*>(&magic),4);
    std::vector<std::pair<uint32_t,uint32_t>> done;uint64_t budget=96ull<<20;
    for(uint32_t i=0;i<s.candidateCount;i++){
        const auto& c=s.candidates[i];
        if((c.kind!=Kind::Compound&&c.kind!=Kind::Mesh)||!c.overlap||c.reused)continue;
        dumpRange(f,done,budget,c.body,0x80);dumpRange(f,done,budget,c.transform.address,64);
        if(c.vtable==0x1040D2E8){dumpMesh(f,done,budget,c.shape);continue;}
        dumpRange(f,done,budget,c.shape,0x100);
        uint32_t data=word(c.shapeBytes,0x20),count=word(c.shapeBytes,0x24);
        if(count>kMaxInstances)continue;
        dumpRange(f,done,budget,data,uint64_t(count)*0x40);
        dumpRange(f,done,budget,word(c.shapeBytes,0x2C),(uint64_t(word(c.shapeBytes,0x30))*2+3)&~3ull);
        dumpRange(f,done,budget,word(c.shapeBytes,0x40),(uint64_t(word(c.shapeBytes,0x44))*8+3)&~3ull);
        for(uint32_t j=0;j<count;j++){Block instance;if(workerBlock(instance,data+j*0x40,0x40))dumpMesh(f,done,budget,word(instance,0x30));}
    }
}
// ---- Direct decoding (worker thread) -------------------------------------
// One copy of each signed root's shape memory, parsed by native_mesh.h. A copy
// is used only after (a) a later harvest reports the same signature again, so
// the header and instances it was parsed from were stable across the copy, and
// (b) the decoder agreed with the game's own keyed triangles on at least
// kProbation of them. Every later native read of the root (the per-region
// audits) is compared again; a single disagreement retires the copy and the
// root goes back to native reads.
constexpr uint32_t kProbation=20;
struct Decoded {
    uint32_t body=0,shape=0,vtable=0,matched=0;uint64_t signature=0,capturedId=0,seen=0;float lo[3]{},hi[3]{};
    nativeMesh::Root root;bool confirmed=false,failed=false;
    bool ready()const{return !failed&&confirmed&&matched>=kProbation;}
};
static std::vector<std::unique_ptr<Decoded>> decodedRoots;
static uint64_t decodeCaptures=0,decodeFailures=0,decodeAudits=0,decodeMismatches=0,decodeUses=0,decodeTriangles=0,decodeMatched=0;
static Decoded* decodedFor(uint32_t body){for(auto& d:decodedRoots)if(d->body==body)return d.get();return nullptr;}
// Whether Minecraft's player collides with a child of a compound or mesh the worker has captured. The game
// asks its group filter about each child with the child's own filter (native_mesh.h); that filter goes through
// the same policy as a body's. Before route-201 only the body's filter was looked at, and every wall the game
// keeps for animals, monsters or the camera alone stood in Steve's way (a shrine's doorway has one). A child
// whose filter is not known here, or is FFFFFFFF (the body's own, judged already), is solid as before.
static uint64_t filteredOut=0,filterUnknown=0,filterAsked=0;
static bool childIncluded(uint32_t body,uint32_t key){
    if(!childFilters.load())return true;
    auto* d=decodedFor(body);if(!d||d->root.children.empty())return true;
    uint32_t filter=0;++filterAsked;
    if(!nativeMesh::keyFilter(d->root,key,filter)){++filterUnknown;return true;}
    if(filter==0xFFFFFFFF||includedLayer(filter))return true;
    ++filteredOut;return false;
}
static bool workerRead(uint32_t address,uint32_t size,uint8_t* out){
    auto* guestBase=static_cast<uint8_t*>(getGuest());SIZE_T copied=0; // body::base is per guest thread
    return guestBase&&ReadProcessMemory(GetCurrentProcess(),guestBase+address,out,size,&copied)&&copied==size;
}
static unsigned decodedReady(){unsigned n=0;for(const auto& d:decodedRoots)n+=d->ready();return n;}
// Names, for a whole-region request, the roots the worker decodes. A root the
// region already holds under the same signature is either skipped as known or
// due for its audit, and an audit is a native read.
static void annotate(nativeRegions::Request& r,const nativeRegions::Entry* e){
    if(r.part!=nativeRegions::kWhole)return;
    float lo[3],hi[3];
    for(int k=0;k<3;k++){lo[k]=nativeRegions::anchor[k]+(r.lo[k]-nativeRegions::origin[k])*nativeRegions::scale;hi[k]=nativeRegions::anchor[k]+(r.hi[k]-nativeRegions::origin[k])*nativeRegions::scale;}
    for(const auto& d:decodedRoots){
        if(!d->ready()||r.decodedCount>=nativeRegions::kKnownRoots)continue;
        bool reach=true,held=false;for(int k=0;k<3;k++)reach&=d->lo[k]<=hi[k]&&d->hi[k]>=lo[k];
        if(e)for(const auto& root:e->statics)held|=root.body==d->body&&root.signature==d->signature;
        if(reach&&!held)r.decoded[r.decodedCount++]={d->body,d->signature};
    }
}
static void poll() {
    if(state.load(std::memory_order_acquire)!=2)return;
    const Snapshot& s=pending;
    if(s.epoch==body::lifecycleEpoch.load())nativeRegions::world.store(s.world);
    bool complete=regionComplete(s);
    // Bodies that move (native_movers.h): those this snapshot lists, and what the harvested region gives of them.
    std::vector<nativeMovers::Seen> moving;nativeMovers::Gathered gathered;std::unordered_map<uint32_t,uint32_t> movingAt;
    const bool movers=nativeMovers::mode.load()&&s.epoch==body::lifecycleEpoch.load();
    if(movers)for(uint32_t i=0;i<s.candidateCount;i++){
        const auto& c=s.candidates[i];if(!c.group||!c.included||c.transform.size<64)continue;
        nativeMovers::Seen seen;seen.body=c.body;seen.shape=c.shape;for(int j=0;j<16;j++)seen.xf[j]=real(c.transform,j*4);
        movingAt[c.body]=uint32_t(moving.size());moving.push_back(seen);
    }
    auto mover=[&](uint32_t body)->const nativeMovers::Seen*{auto found=movingAt.find(body);return found==movingAt.end()?nullptr:&moving[found->second];};
    if(s.region){
        std::vector<botwcraft::proto::ColTri> triangles;nativeRegions::Roots roots;
        // Mesh triangles taken from the decoder instead of the harvest, by body.
        std::vector<std::pair<uint32_t,std::vector<nativeMesh::Keyed>>> decodedTriangles;
        const uint64_t tick=GetTickCount64();
        for(uint32_t i=0;i<s.candidateCount&&s.epoch==body::lifecycleEpoch.load();i++){
            const auto& c=s.candidates[i];
            if((c.kind!=Kind::Compound&&c.kind!=Kind::Mesh)||!c.overlap||!c.signature||!c.bounded)continue;
            auto* d=decodedFor(c.body);
            if(!d||d->signature!=c.signature){
                if(!d){
                    if(decodedRoots.size()>=64){auto oldest=std::min_element(decodedRoots.begin(),decodedRoots.end(),[](const auto& a,const auto& b){return a->seen<b->seen;});decodedRoots.erase(oldest);}
                    decodedRoots.push_back(std::make_unique<Decoded>());d=decodedRoots.back().get();
                }
                *d=Decoded{};d->body=c.body;d->shape=c.shape;d->vtable=c.vtable;d->signature=c.signature;d->capturedId=s.id;
                d->failed=!nativeMesh::capture(d->root,c.shape,c.vtable,workerRead);++decodeCaptures;decodeFailures+=d->failed;
                // lab/compound-dump.txt 1: the children of a captured compound, one line each, with the filter, the
                // filter's mask and the user data each instance carries (lab/compound-BODY.csv).
                if(!d->failed&&c.vtable==nativeMesh::kCompound){
                    int dump=0;std::ifstream(directory+"/compound-dump.txt")>>dump;
                    if(dump==1){
                        using nativeMesh::be32;using nativeMesh::bef;
                        auto h=d->root.image.at(c.shape,0x100);uint32_t data=be32(h+0x20),count=be32(h+0x24);
                        std::ofstream f(directory+"/compound-"+std::to_string(c.body)+".csv");f.precision(9);f<<"index,flags,child,vtable,type,filter,mask,user,x,y,z,qx,qy,qz,qw,sx,sy,sz,tw,sw,head\n";
                        for(uint32_t n=0;n<count;n++){
                            auto raw=d->root.image.at(data+n*0x40,0x40);if(!raw)break;
                            uint32_t child=be32(raw+0x30);auto head=d->root.image.at(child,0x10);
                            f<<n<<','<<(be32(raw+0x0C)&0x7F)<<','<<child<<','<<(head?be32(head):0)<<','<<(head?int(head[8]):-1)<<','<<be32(raw+0x34)<<','<<be32(raw+0x38)<<','<<be32(raw+0x3C)<<','<<bef(raw)<<','<<bef(raw+4)<<','<<bef(raw+8);
                            for(int k=0;k<7;k++)f<<','<<bef(raw+0x10+k*4+(k>=4?0:0));
                            f<<','<<be32(raw+0x0C)<<','<<be32(raw+0x2C)<<',';
                            if(auto more=d->root.image.at(child,0x70)){const char* hex="0123456789abcdef";for(int k=0x10;k<0x70;k++)f<<hex[more[k]>>4]<<hex[more[k]&15];}
                            f<<"\n";
                        }
                        // The meshes among them (lab/meshes-BODY.csv): the palettes of filters and of user data, and
                        // the runs that give each primitive its place in them, as raw bytes.
                        std::ofstream m(directory+"/meshes-"+std::to_string(c.body)+".csv");m<<"child,flags,filters,users,sections,primitives,runs,run_bytes,section_words\n";
                        std::set<uint32_t> told;const char* hex="0123456789abcdef";
                        auto bytes=[&](uint32_t address,uint32_t size){std::vector<uint8_t> out(size);if(!size||!workerRead(address,size,out.data()))out.clear();return out;};
                        for(uint32_t n=0;n<count;n++){
                            auto raw=d->root.image.at(data+n*0x40,0x40);if(!raw)break;
                            uint32_t child=be32(raw+0x30);auto head=d->root.image.at(child,0x100);
                            if(!head||be32(head)!=nativeMesh::kMesh||!told.insert(child).second)continue;
                            m<<child<<','<<be32(head+0x1C)<<',';
                            for(uint32_t offset:{0x20u,0x2Cu}){auto palette=bytes(be32(head+offset),std::min(be32(head+offset+4),64u)*4);for(size_t k=0;k+4<=palette.size();k+=4)m<<(k?" ":"")<<be32(palette.data()+k);m<<',';}
                            uint32_t sections=be32(head+0x90),runs=be32(head+0xCC);
                            m<<sections<<','<<be32(head+0x9C)<<','<<runs<<',';
                            auto run=bytes(be32(head+0xC8),std::min(runs,48u)*4);for(uint8_t v:run)m<<hex[v>>4]<<hex[v&15];
                            m<<',';
                            auto section=bytes(be32(head+0x8C),std::min(sections,6u)*0x60);
                            for(size_t k=0;k+0x60<=section.size();k+=0x60){m<<(k?" ":"");for(int w=0x48;w<0x60;w++)m<<hex[section[k+w]>>4]<<hex[section[k+w]&15];}
                            m<<"\n";
                        }
                    }
                }
                event("{\"event\":\"decode_capture\",\"body\":%u,\"vtable\":%u,\"ok\":%d,\"bytes\":%llu,\"instances\":%zu,\"meshes\":%zu}",c.body,c.vtable,int(!d->failed),(unsigned long long)d->root.image.bytes,d->root.instances.size(),d->root.meshes.size());
            }else if(!d->confirmed&&s.id!=d->capturedId)d->confirmed=true;
            d->seen=tick;std::copy_n(c.lo,3,d->lo);std::copy_n(c.hi,3,d->hi);
            float xf[16];for(int j=0;j<16;j++)xf[j]=real(c.transform,j*4);
            std::vector<nativeMesh::Keyed> mine;
            if(c.decoded){
                // The harvest left this root's mesh triangles to the decoder.
                if(!d->ready()||!nativeMesh::region(d->root,xf,s.queryLo,s.queryHi,mine)){complete=false;d->failed=true;++decodeFailures;continue;}
                ++decodeUses;decodedTriangles.emplace_back(c.body,std::move(mine));
            }else if(!d->failed&&!c.reused&&(c.done||c.partial)){
                // Native read of a root the worker holds: audit the decoder.
                if(!nativeMesh::region(d->root,xf,s.queryLo,s.queryHi,mine)){d->failed=true;++decodeFailures;continue;}
                std::vector<nativeMesh::Keyed> native;
                for(uint32_t t=0;t<s.triangleCount;t++)if(s.triangles[t].body==c.body){nativeMesh::Keyed k;k.key=s.triangles[t].key;std::copy_n(s.triangles[t].v,9,k.v);native.push_back(k);}
                const bool whole=c.done&&!c.partial&&!c.chunk;
                auto r=nativeMesh::compare(mine,native,s.queryLo,s.queryHi,whole);++decodeAudits;
                if(!r.ok()){
                    d->failed=true;++decodeMismatches;
                    event("{\"event\":\"decode_mismatch\",\"body\":%u,\"id\":%llu,\"matched\":%u,\"missing\":%u,\"moved\":%u,\"extra\":%u}",c.body,s.id,r.matched,r.missing,r.moved,r.extra);
                }else{
                    d->matched+=r.matched;decodeMatched+=r.matched;
                    // A whole, agreeing read keeps the decoder's triangles, so what the
                    // region holds stays bit-identical between decoded and audited reads.
                    if(whole&&d->ready())decodedTriangles.emplace_back(c.body,std::move(mine));
                }
            }
        }
        auto fromDecoder=[&](uint32_t body)->std::vector<nativeMesh::Keyed>*{for(auto& [b,t]:decodedTriangles)if(b==body)return &t;return nullptr;};
        for(uint32_t i=0;i<s.candidateCount;i++){
            const auto& c=s.candidates[i];
            if((c.kind!=Kind::Compound&&c.kind!=Kind::Mesh)||!c.overlap||!c.signature)continue;
            if(c.reused)roots.reused.push_back({c.body,c.signature});
            else if(c.done||c.partial)roots.fresh.push_back({c.body,c.signature,{},c.chunk,c.partial,0,fromDecoder(c.body)!=nullptr});
        }
        roots.continued=s.continued;
        // What the game's material word says of a mesh's triangle (nativeMesh::keyUser): wall codes 3, 4 and 6 are
        // LadderUp, Ladder and LadderSide in the decompilation's physDefines.h. Minecraft's player climbs such a
        // triangle as a Minecraft ladder (report 75). Known only for a root the worker has captured.
        uint32_t ladderCount=0,ladderBody=0,noClimbCount=0;float ladderLo[3]{1e30f,1e30f,1e30f},ladderHi[3]{-1e30f,-1e30f,-1e30f};
        auto marks=[&](uint32_t body,uint32_t key,botwcraft::proto::ColTri& t){
            auto* d=decodedFor(body);uint32_t user=0;if(!d||!nativeMesh::keyUser(d->root,key,user))return;
            // Wall codes 1, 5 and 8 are NoClimb, Slip and NoDashUpAndNoClimb there: Minecraft's player, who climbs the
            // game's walls (physical.h, climbStamina), does not climb these. Nor the stone of the game's shrines
            // (material 2, stone, with sub-material 5 or 6): route-361, the patterned walls of the Shrine of Resurrection
            // carry wall code 0 and sub-material 5, the rock its player has to climb there sub-material 2.
            t.flags|=botwcraft::proto::kTriUserKnown|(user>>15&31)<<botwcraft::proto::kTriWallShift|(user&0x3FF)<<botwcraft::proto::kTriUserShift;
            const uint32_t wall=user>>15&31,material=user&63,sub=user>>6&15;if(wall==1||wall==5||wall==8||(material==2&&(sub==5||sub==6))){t.flags|=botwcraft::proto::kTriNoClimb;++noClimbCount;}
            if(wall!=3&&wall!=4&&wall!=6)return;
            t.flags|=botwcraft::proto::kTriLadder;++ladderCount;ladderBody=body;
            for(int j=0;j<9;j++){ladderLo[j%3]=std::min(ladderLo[j%3],t.v[j]);ladderHi[j%3]=std::max(ladderHi[j%3],t.v[j]);}
        };
        // A fixed root's triangles stay with it, so later harvests can skip it.
        auto owner=[&](uint32_t body)->std::vector<botwcraft::proto::ColTri>&{
            for(auto& f:roots.fresh)if(f.body==body)return f.triangles;
            return triangles;
        };
        for(uint32_t i=0;i<s.triangleCount;i++){
            botwcraft::proto::ColTri t{};
            for(int j=0;j<9;j++)t.v[j]=(s.triangles[i].v[j]-nativeRegions::anchor[j%3])/nativeRegions::scale+nativeRegions::origin[j%3];
            if(!nativeGeometry::valid(t))continue;
            // An audited root keeps the decoder's twin of each of these; a root
            // the harvest left to the decoder only returned other children here.
            bool audited=false;for(uint32_t j=0;j<s.candidateCount&&!audited;j++)audited=s.candidates[j].body==s.triangles[i].body&&!s.candidates[j].decoded&&fromDecoder(s.triangles[i].body);
            marks(s.triangles[i].body,s.triangles[i].key,t);
            if(auto* m=mover(s.triangles[i].body)){if(childIncluded(s.triangles[i].body,s.triangles[i].key))nativeMovers::gather(gathered,*m,s.triangles[i].key,t);continue;}
            if(!audited&&nativeGeometry::overlaps(t,s.region->lo,s.region->hi)&&childIncluded(s.triangles[i].body,s.triangles[i].key))owner(s.triangles[i].body).push_back(t);
        }
        for(auto& [body,keyed]:decodedTriangles)for(const auto& k:keyed){
            botwcraft::proto::ColTri t{};
            for(int j=0;j<9;j++)t.v[j]=(k.v[j]-nativeRegions::anchor[j%3])/nativeRegions::scale+nativeRegions::origin[j%3];
            if(nativeGeometry::valid(t)&&nativeGeometry::overlaps(t,s.region->lo,s.region->hi)&&childIncluded(body,k.key)){marks(body,k.key,t);owner(body).push_back(t);++decodeTriangles;}
        }
        std::vector<botwcraft::proto::ColTri> terrain;terrainTriangles(s.terrain,terrain);
        for(const auto& t:terrain)if(nativeGeometry::overlaps(t,s.region->lo,s.region->hi))triangles.push_back(t);
        std::vector<botwcraft::proto::ColTri> boxes;
        for(uint32_t i=0;i<s.candidateCount;i++){
            const auto& c=s.candidates[i];
            if(c.kind!=Kind::Fallback||!c.overlap||c.pieces)continue;
            if(auto* m=mover(c.body)){
                std::vector<botwcraft::proto::ColTri> own;if(!orientedBox(c,own))boxTriangles(c.lo,c.hi,own);
                for(const auto& t:own)nativeMovers::gather(gathered,*m,nativeMovers::kBounds<<32,t);
                continue;
            }
            if(!orientedBox(c,boxes))boxTriangles(c.lo,c.hi,boxes);
        }
        for(const auto& t:boxes)if(nativeGeometry::overlaps(t,s.region->lo,s.region->hi))triangles.push_back(t);
        // The convex pieces: each one's hull; a piece whose points make no solid is the box around them.
        for(uint32_t i=0;i<s.pieceCount;i++){
            const auto& piece=s.pieces[i];const float* points=s.piecePoints.data()+piece.first*3;boxes.clear();
            if(piece.key!=~0u&&!childIncluded(piece.body,piece.key))continue;
            if(!pieceTrianglesOf(points,piece.count,boxes)){
                float lo[3]{1e30f,1e30f,1e30f},hi[3]{-1e30f,-1e30f,-1e30f};
                for(uint32_t q=0;q<piece.count;q++)for(int k=0;k<3;k++){lo[k]=std::min(lo[k],points[q*3+k]);hi[k]=std::max(hi[k],points[q*3+k]);}
                boxTriangles(lo,hi,boxes);
            }
            if(auto* m=mover(piece.body)){for(const auto& t:boxes)nativeMovers::gather(gathered,*m,nativeMovers::kPieces<<32|piece.key,t);continue;}
            for(const auto& t:boxes)if(nativeGeometry::overlaps(t,s.region->lo,s.region->hi))owner(piece.body).push_back(t);
        }
        for(uint32_t i=0;i<s.leafBoxCount;i++){
            if(!childIncluded(s.leafBoxes[i].body,s.leafBoxes[i].key))continue;
            boxes.clear();boxTriangles(s.leafBoxes[i].lo,s.leafBoxes[i].hi,boxes);
            if(auto* m=mover(s.leafBoxes[i].body)){for(const auto& t:boxes)nativeMovers::gather(gathered,*m,nativeMovers::kLeafBoxes<<32|s.leafBoxes[i].key,t);continue;}
            for(const auto& t:boxes)if(nativeGeometry::overlaps(t,s.region->lo,s.region->hi))owner(s.leafBoxes[i].body).push_back(t);
        }
        // Completeness covers acquisition only. Publication, acknowledgement and
        // the movement lease are separate gates in nativeStream/walking.
        {static uint64_t told=0;
            {static uint64_t toldNo=0;if(noClimbCount&&GetTickCount64()-toldNo>=2000){toldNo=GetTickCount64();event("{\"event\":\"native_ladders\",\"no_climb_triangles\":%u,\"region\":[%d,%d,%d]}",noClimbCount,s.region->key[0],s.region->key[1],s.region->key[2]);}}
            if(ladderCount&&GetTickCount64()-told>=500){told=GetTickCount64();
                event("{\"event\":\"native_ladders\",\"body\":%u,\"triangles\":%u,\"lo\":[%.3f,%.3f,%.3f],\"hi\":[%.3f,%.3f,%.3f],\"region\":[%d,%d,%d]}",ladderBody,ladderCount,ladderLo[0],ladderLo[1],ladderLo[2],ladderHi[0],ladderHi[1],ladderHi[2],s.region->key[0],s.region->key[1],s.region->key[2]);}}
        nativeRegions::cache.finish(*s.region,complete,std::move(triangles),GetTickCount64(),s.budgetExceeded,std::move(roots));
        {static uint64_t told=0,last=0;
            if(filteredOut!=last&&GetTickCount64()-told>=1000){told=GetTickCount64();last=filteredOut;event("{\"event\":\"native_filtered\",\"children_asked\":%llu,\"not_for_the_player\":%llu,\"filter_unknown\":%llu}",filterAsked,filteredOut,filterUnknown);}}
    }
    // A body that has no part yet (no harvested region has reached it: route-271, a box that fell was first sent 2 m
    // into its fall) stands in as a box at once: the exact one of a box shape, else the box of its bounds if that is
    // small (a large one may be hollow, with Steve in it). Its own pieces or triangles replace it when they are read.
    if(movers)for(uint32_t i=0;i<s.candidateCount;i++){
        const auto& c=s.candidates[i];auto* m=mover(c.body);if(!m||!c.bounded||gathered.count(c.body))continue;
        auto held=nativeMovers::movers.find(c.body);
        if(held!=nativeMovers::movers.end()&&held->second.shape==c.shape&&!held->second.parts.empty())continue;
        std::vector<botwcraft::proto::ColTri> own;
        if(!orientedBox(c,own)){
            if(std::max({c.hi[0]-c.lo[0],c.hi[1]-c.lo[1],c.hi[2]-c.lo[2]})>nativeMovers::kStandIn)continue;
            boxTriangles(c.lo,c.hi,own);
        }
        for(const auto& t:own)nativeMovers::gather(gathered,*m,nativeMovers::kBounds<<32,t);
    }
    if(movers)nativeMovers::update(moving,gathered,s.inventoryComplete);
    // Rolling acquisition retains eight replaceable diagnostics; manual probes
    // keep their request-named files. Do not grow one file per travelling frame.
    if(quietTrace.load()){state.store(0,std::memory_order_release);return;} // no harvest diagnostics while playing
    static uint32_t diagnosticSlot=0;
    std::string name=s.region?"native-region-slot-"+std::to_string(diagnosticSlot++%8):"native-shapes-"+std::to_string(s.id);
    static uint64_t dumped=0;uint64_t token=0;
    if(s.region&&s.triangleCount>=200){std::ifstream(directory+"/mesh-dump.txt")>>token;}
    if(token&&token!=dumped){
        dumped=token;name="mesh-dump-"+std::to_string(token);dumpRoots(s,directory+"/"+name+".bin");
        event("{\"event\":\"mesh_dump\",\"token\":%llu,\"id\":%llu,\"triangles\":%u}",token,s.id,s.triangleCount);
    }
    std::ofstream f(directory+"/"+name+".json");
    f.precision(9);
    f<<"{\"id\":"<<s.id<<",\"epoch\":"<<s.epoch<<",\"current_epoch\":"<<body::lifecycleEpoch.load()
     <<",\"qpc\":"<<s.qpc<<",\"ticks\":"<<s.ticks<<",\"layer\":"<<s.layer<<",\"system\":"<<s.system
     <<",\"world\":"<<s.world<<",\"status\":\"region-harvest\",\"coverage_complete\":"<<(complete?"true":"false")
     <<",\"excluded\":"<<s.excluded<<",\"fallbacks\":"<<s.fallbacks<<",\"unboundable\":"<<s.unboundable
     <<",\"mesh_roots\":"<<s.meshRoots<<",\"heightfields\":"<<s.heightfields<<",\"key_fault_reason\":"<<s.keyFaultReason
     <<",\"leaf_boxes\":"<<s.leafBoxCount<<",\"pieces\":"<<s.pieceCount<<",\"piece_points\":"<<s.piecePointCount<<",\"piece_failures\":"<<s.pieceFailures<<",\"piece_failed\":[";
    for(uint32_t i=0;i<s.pieceFailureCount;i++){const auto& u=s.pieceFailed[i];if(i)f<<',';
        f<<"{\"body\":"<<u.body<<",\"shape\":"<<u.shape<<",\"vtable\":"<<u.vtable<<",\"type\":"<<unsigned(u.type)<<",\"reason\":"<<unsigned(u.reason)<<'}';}
    f<<"],\"piece_list\":[";
    for(uint32_t i=0;i<s.pieceCount&&i<64;i++){const auto& u=s.pieces[i];if(i)f<<',';f<<"{\"body\":"<<u.body<<",\"points\":[";
        for(uint32_t q=0;q<u.count*3;q++){if(q)f<<',';f<<s.piecePoints[u.first*3+q];}f<<"]}";}
    f<<"],\"unknown\":[";
    for(uint32_t i=0;i<s.unknownCount;i++){const auto& u=s.unknown[i];if(i)f<<',';
        f<<"{\"body\":"<<u.body<<",\"key\":"<<u.key<<",\"vtable\":"<<u.vtable<<",\"type\":"<<unsigned(u.type)<<",\"reason\":"<<unsigned(u.reason)<<",\"block\":"<<int(u.block)<<'}';}
    f<<"],\"leaf_box_examples\":[";
    for(uint32_t i=0;i<s.leafBoxCount&&i<16;i++){const auto& b=s.leafBoxes[i];if(i)f<<',';
        f<<"{\"body\":"<<b.body<<",\"key\":"<<b.key<<",\"vtable\":"<<b.vtable<<",\"type\":"<<unsigned(b.type)<<",\"lo\":["<<b.lo[0]<<','<<b.lo[1]<<','<<b.lo[2]<<"],\"hi\":["<<b.hi[0]<<','<<b.hi[1]<<','<<b.hi[2]<<"]}";}
    f<<"]"
     <<",\"owned_lock_depth\":"<<s.ownedLockDepth
     <<",\"terrain_grid\":{\"shape\":"<<s.terrain.shape<<",\"reader\":"<<s.terrain.reader<<",\"x\":"<<s.terrain.x<<",\"z\":"<<s.terrain.z
     <<",\"width\":"<<s.terrain.width<<",\"depth\":"<<s.terrain.depth<<",\"spacing\":"<<s.terrain.spacing
     <<",\"offset\":["<<s.terrain.offset[0]<<','<<s.terrain.offset[1]<<"],\"read\":"<<s.terrain.read<<",\"available\":"<<s.terrain.available
     <<",\"complete\":"<<(s.terrain.complete?"true":"false")<<",\"samples\":[";
    for(uint32_t i=0;i<s.terrain.read;i++){if(i)f<<',';if(s.terrain.valid[i])f<<s.terrain.height[i];else f<<"null";}
    f<<"]}"
     <<",\"queries\":"<<s.queries<<",\"saturated_queries\":"<<s.saturatedQueries<<",\"key_capacity\":"<<kKeyCapacity<<",\"max_keys\":"<<s.maxKeys<<",\"key_faults\":"<<s.keyFaults
     <<",\"reused_roots\":"<<s.reusedRoots<<",\"queried_roots\":"<<s.queriedRoots<<",\"done_roots\":"<<s.doneRoots<<",\"continued\":"<<(s.continued?"true":"false")<<",\"decoded_roots\":"<<s.decodedRoots<<",\"dropped_keys\":"<<s.droppedKeys
     <<",\"compound_complete\":"<<(s.compoundComplete?"true":"false")
     <<",\"query_budget_exceeded\":"<<(s.budgetExceeded?"true":"false")<<",\"unknown_leaves\":"<<s.unknownLeaves
     <<",\"entity_start\":"<<s.entityStart<<",\"entity_total\":"<<s.entityTotal<<",\"selected\":"<<s.selected
     <<",\"height_shape\":"<<s.heightShape<<",\"height_reader\":"<<s.heightReader<<",\"height_available\":"<<s.heightAvailable
     <<",\"height\":";if(s.heightAvailable==1 && std::isfinite(s.height))f<<s.height;else f<<"null";
    f
     <<",\"key_count\":"<<s.keyCount<<",\"leaves\":"<<s.leaves<<",\"query_lo\":["<<s.queryLo[0]<<','<<s.queryLo[1]<<','<<s.queryLo[2]
     <<"],\"query_hi\":["<<s.queryHi[0]<<','<<s.queryHi[1]<<','<<s.queryHi[2]<<"],\"leaf_copies\":[";
    for(uint32_t i=0;i<s.leaves;i++){if(i)f<<',';f<<"{\"block\":"<<s.leafBlocks[i]<<",\"key\":"<<s.leafKeys[i]<<'}';}
    f<<"],\"key_buffer_saturated\":"<<(s.keyCount>=kKeyCapacity?"true":"false")<<",\"blocks\":[";
    for(uint32_t i=0;i<s.count;i++) {
        if(i)f<<',';
        const Block& b=s.blocks[i];f<<"{\"address\":"<<b.address<<",\"size\":"<<b.size<<",\"hex\":\"";
        const char* hex="0123456789abcdef";for(uint32_t j=0;j<b.size;j++)f<<hex[b.bytes[j]>>4]<<hex[b.bytes[j]&15];f<<"\"}";
    }
    f<<"],\"triangles\":[";
    for(uint32_t i=0;i<s.triangleCount;i++){
        if(i)f<<',';
        const auto& t=s.triangles[i];f<<"{\"body\":"<<t.body<<",\"key\":"<<t.key<<",\"radius\":"<<t.radius<<",\"vertices\":[";
        for(int k=0;k<9;k++){if(k)f<<',';f<<t.v[k];}f<<"]}";
    }
    f<<"],\"inventory\":{\"complete\":"<<(s.inventoryComplete?"true":"false")
     <<",\"failure\":"<<s.inventoryFailure<<",\"failure_address\":"<<s.failureAddress<<",\"empty_fixed\":"<<s.emptyFixed
     <<",\"islands\":["<<s.islands[0]<<','<<s.islands[1]<<','<<s.islands[2]
     <<"],\"bodies\":["<<s.bodies[0]<<','<<s.bodies[1]<<','<<s.bodies[2]
     <<"],\"bounds_read\":"<<s.boundsRead<<",\"unsupported\":"<<s.unsupported<<",\"candidates\":[";
    for(uint32_t i=0;i<s.candidateCount;i++){
        if(i)f<<',';const auto& c=s.candidates[i];
        f<<"{\"body\":"<<c.body<<",\"island\":"<<c.island<<",\"group\":"<<c.group<<",\"filter\":"<<c.filter
         <<",\"layer\":"<<layerOf(c.filter)<<",\"included\":"<<(c.included?"true":"false")<<",\"kind\":"<<int(c.kind)<<",\"aabb_fn\":"<<c.aabbFn
         <<",\"shape\":"<<c.shape<<",\"vtable\":"<<c.vtable<<",\"type\":"<<unsigned(c.shapeBytes.bytes[8])
         <<",\"bounded\":"<<(c.bounded?"true":"false")<<",\"overlap\":"<<(c.overlap?"true":"false")
         <<",\"signature\":\""<<c.signature<<"\",\"reused\":"<<(c.reused?"true":"false")<<",\"done\":"<<(c.done?"true":"false")<<",\"partial\":"<<(c.partial?"true":"false")<<",\"chunk\":"<<c.chunk<<",\"decoded\":"<<(c.decoded?"true":"false")<<",\"pieces\":"<<(c.pieces?"true":"false");
        if(c.bounded)f<<",\"lo\":["<<c.lo[0]<<','<<c.lo[1]<<','<<c.lo[2]<<"],\"hi\":["<<c.hi[0]<<','<<c.hi[1]<<','<<c.hi[2]<<']';
        f<<",\"transform\":[";for(int j=0;j<16;j++){if(j)f<<',';f<<real(c.transform,j*4);}f<<"]}";
    }
    f<<"]}}";f.close();
    event("{\"event\":\"native_shape_probe\",\"id\":%llu,\"epoch\":%llu,\"world\":%u,\"blocks\":%u,\"ticks\":%llu,\"ms\":%.4f,\"queries\":%u,\"saturated\":%u,\"max_keys\":%u,\"key_faults\":%u,\"triangles\":%u,\"terrain_read\":%u,\"terrain_available\":%u,\"terrain_complete\":%d,\"budget_exceeded\":%d,\"complete\":%d,\"unsupported\":%u,\"fallbacks\":%u,\"excluded\":%u,\"unboundable\":%u,\"unknown_leaves\":%u,\"leaf_boxes\":%u,\"key_fault_reason\":%u,\"over_capacity\":%u,\"terrain_loaded\":%d,\"stale\":%d,\"reused_roots\":%u,\"queried_roots\":%u,\"part\":%u,\"known\":%u,\"done_roots\":%u,\"continued\":%d,\"decoded_roots\":%u,\"dropped_keys\":%u}",s.id,s.epoch,s.world,s.count,s.ticks,frequency.QuadPart?s.ticks*1000./frequency.QuadPart:0.,s.queries,s.saturatedQueries,s.maxKeys,s.keyFaults,s.triangleCount,s.terrain.read,s.terrain.available,int(s.terrain.complete),int(s.budgetExceeded),int(complete),s.unsupported,s.fallbacks,s.excluded,s.unboundable,s.unknownLeaves,s.leafBoxCount,s.keyFaultReason,s.overCapacity,int(terrainLoaded(s)),int(s.epoch!=body::lifecycleEpoch.load()),s.reusedRoots,s.queriedRoots,unsigned(s.region?s.region->part:0xFF),unsigned(s.region?s.region->knownCount:0),s.doneRoots,int(s.continued),s.decodedRoots,s.droppedKeys);
    state.store(0,std::memory_order_release);
}
}
