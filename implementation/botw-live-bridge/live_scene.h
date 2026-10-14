// Owned v12 presentation snapshots. Only the IPC worker mutates CPU state.
#pragma once
#include "../botwcraft-fabric/protocol/botwcraft_protocol.h"
#include <map>
#include <tuple>
namespace presentation {
namespace p=botwcraft::proto;
using Key=std::tuple<int32_t,int32_t,int32_t>;
struct Rect {uint32_t x=0,y=0,w=0,h=0;};
// `dirty` (atlas only): the rectangles in which this atlas differs from the
// published atlas whose version is `base`; base 0 means "everything".
// Meshes: `data` is RenVertex rows only. A section's rows are its opaque and
// cutout triangles followed by its translucent ones (`split` vertices in the
// first group); an entity mesh has `batches` and, for the scene, the Minecraft
// block its positions are relative to.
struct Bytes {uint64_t version=0,hash=0;std::vector<uint8_t> data;uint64_t base=0;std::vector<Rect> dirty;
    uint32_t split=0;std::vector<p::RenBatch> batches;double origin[3]{};};
using Blob=std::shared_ptr<const Bytes>;
// Arrows, dropped items, block cracks and the block outline, rebuilt from the
// world-entity table: RenVertex rows relative to `origin`, in three groups.
struct Dynamic {uint64_t version=0;double origin[3]{};uint32_t solid=0,cracks=0,outline=0;std::vector<uint8_t> data;};
struct Scene {uint64_t session=0,clear=0,version=0;Blob atlas;std::map<Key,Blob> sections;size_t meshBytes=0;
    std::map<uint32_t,Blob> textures;size_t textureBytes=0;Blob entities,avatar;std::shared_ptr<const Dynamic> dynamic;};
struct Hud {uint64_t session=0,frame=0,hash=0;uint32_t w=0,h=0;std::vector<uint8_t> texture;};
static Scene working;
static std::atomic<std::shared_ptr<const Scene>> scene;
static std::atomic<std::shared_ptr<const Hud>> hud;
static std::atomic<bool> enabled{false},eligible{false};
static uint64_t serial=0;
static std::shared_ptr<Bytes> atlasEdit;static uint64_t atlasPublishAt=0;
// Vertex flags (protocol: 1 cutout, 2 translucent, bits 4-6 normal). 4, 8 and
// the line bits are SkyCraft's and this renderer's own, set here only.
constexpr uint32_t kCutout=1,kTranslucent=2,kUntextured=4,kNoMip=8,kNormalFromFaces=7u<<4,kLine=1u<<8,kLineFlip=1u<<9;
constexpr uint32_t kFullSky=15u<<8;
static uint64_t hash(const void* ptr,size_t n){uint64_t h=14695981039346656037ULL;auto p=static_cast<const uint8_t*>(ptr);for(size_t i=0;i<n;i++)h=(h^p[i])*1099511628211ULL;return h;}
static Blob own(const void* p,size_t n){auto b=std::make_shared<Bytes>();b->version=++serial;b->data.assign(static_cast<const uint8_t*>(p),static_cast<const uint8_t*>(p)+n);b->hash=hash(p,n);return b;}
static void reset(uint64_t session){working={};atlasEdit.reset();working.session=session;working.clear=++serial;scene.store(std::make_shared<Scene>(working));hud.store(nullptr);eligible=false;}
// Opaque/cutout triangles first, translucent (water, stained glass, ice) after: WorldRender.cpp OnSection.
static Blob section(const uint8_t* rows,uint32_t count){
    auto b=std::make_shared<Bytes>();b->version=++serial;b->data.resize(size_t(count)*sizeof(p::RenVertex));size_t at=0;
    for(int pass=0;pass<2;pass++){
        for(uint32_t t=0;t+3<=count;t+=3){
            uint32_t flags;memcpy(&flags,rows+size_t(t)*sizeof(p::RenVertex)+offsetof(p::RenVertex,flags),4);
            if(bool(flags&kTranslucent)==(pass==1)){memcpy(b->data.data()+at,rows+size_t(t)*sizeof(p::RenVertex),3*sizeof(p::RenVertex));at+=3*sizeof(p::RenVertex);}
        }
        if(pass==0)b->split=uint32_t(at/sizeof(p::RenVertex));
    }
    b->data.resize(at);b->hash=hash(rows,size_t(count)*sizeof(p::RenVertex));return b;
}
// RenScene/RenAvatar/(header) + RenBatch[] + RenVertex[]; already validated by live::mesh.
static Blob batches(const uint8_t* data,size_t n,size_t header,uint32_t batchCount,uint32_t vertexCount){
    if(!batchCount||!vertexCount)return nullptr;
    auto b=std::make_shared<Bytes>();b->version=++serial;b->batches.resize(batchCount);memcpy(b->batches.data(),data+header,size_t(batchCount)*sizeof(p::RenBatch));
    size_t start=header+size_t(batchCount)*sizeof(p::RenBatch);b->data.assign(data+start,data+n);return b;
}
// Steve's body standing still, facing +z, feet at the origin, in Minecraft's six parts (kRenRagdoll, about once a
// second; SkyCraft hangs it on Skyrim's ragdoll). Here it is what stands in Link's place in a scene of the game's.
static Blob standingBody;static bool sceneBody=false;
static bool accept(uint32_t type,const uint8_t* data,size_t n){
    switch(type){
    case p::kRenClearAll:working.sections.clear();working.meshBytes=0;working.atlas.reset();atlasEdit.reset();working.textures.clear();working.textureBytes=0;working.entities.reset();working.avatar.reset();working.clear=++serial;hud.store(nullptr);break;
    case p::kRenAtlas:atlasEdit.reset();working.atlas=own(data,n);break;
    case p::kRenAtlasRegion:{
        if(!working.atlas)return false;
        p::RenAtlasRegion region;memcpy(&region,data,sizeof(region));p::RenAtlas a;memcpy(&a,working.atlas->data.data(),sizeof(a));
        if(region.x>a.width||region.y>a.height||region.width>a.width-region.x||region.height>a.height-region.y||n<16+size_t(region.width)*region.height*4)return false;
        if(!atlasEdit){atlasEdit=std::make_shared<Bytes>(*working.atlas);atlasEdit->base=working.atlas->version;atlasEdit->dirty.clear();}
        auto next=atlasEdit;next->version=++serial;
        if(next->dirty.size()<256)next->dirty.push_back({region.x,region.y,region.width,region.height});else next->base=0;
        for(uint32_t y=0;y<region.height;y++)memcpy(next->data.data()+8+4*(size_t(region.y+y)*a.width+region.x),data+16+size_t(y)*region.width*4,size_t(region.width)*4);
        next->hash=(next->hash^hash(data,n))*1099511628211ULL;break; // ordered provenance, not a digest of the final atlas pixels
    }
    case p::kRenSection:{
        p::RenSection s;memcpy(&s,data,sizeof(s));Key key{s.sx,s.sy,s.sz};auto found=working.sections.find(key);
        size_t old=found==working.sections.end()?0:found->second->data.size();size_t next=working.meshBytes-old+(s.vertexCount?n-sizeof(s):0);
        // A bounded first area. Capacity overflow faults the session instead of losing a delete/bootstrap.
        if(next>(64u<<20)||(found==working.sections.end()&&s.vertexCount&&working.sections.size()>=512))return false;
        working.meshBytes=next;if(s.vertexCount)working.sections[key]=section(data+sizeof(s),s.vertexCount);else working.sections.erase(key);break;
    }
    case p::kRenTexture:{
        // An entity texture (skins, mobs, particles): width, height, RGBA rows, as the atlas is kept.
        p::RenTexture t;memcpy(&t,data,sizeof(t));auto found=working.textures.find(t.id);
        size_t old=found==working.textures.end()?0:found->second->data.size(),bytes=8+size_t(t.width)*t.height*4,next=working.textureBytes-old+bytes;
        if(next>(96u<<20)||(found==working.textures.end()&&working.textures.size()>=1024))return false;
        auto b=std::make_shared<Bytes>();b->version=++serial;b->data.resize(bytes);memcpy(b->data.data(),&t.width,4);memcpy(b->data.data()+4,&t.height,4);memcpy(b->data.data()+8,data+sizeof(t),bytes-8);
        b->hash=hash(data+sizeof(t),bytes-8);working.textureBytes=next;working.textures[t.id]=b;break;
    }
    case p::kRenScene:{
        p::RenScene s;memcpy(&s,data,sizeof(s));auto b=std::const_pointer_cast<Bytes>(batches(data,n,sizeof(s),s.batchCount,s.vertexCount));
        if(b){b->origin[0]=s.originX;b->origin[1]=s.originY;b->origin[2]=s.originZ;}
        if(!b&&!working.entities)return true;
        working.entities=b;break;
    }
    case p::kRenAvatar:{
        if(sceneBody)return true;   // the scene's body is drawn, not Minecraft's third person
        p::RenAvatar a;memcpy(&a,data,sizeof(a));auto b=batches(data,n,sizeof(a),a.batchCount,a.vertexCount);
        if(!b&&!working.avatar)return true;
        working.avatar=b;break;
    }
    case p::kRenRagdoll:{p::RenAvatar a;memcpy(&a,data,sizeof(a));if(auto b=batches(data,n,sizeof(a),a.batchCount,a.vertexCount))standingBody=b;return true;}
    default:return true; // lights, solids and dug cells are validated and drained, not drawn
    }
    working.version=++serial;return true;
}
static void publish(){if(atlasEdit&&GetTickCount64()-atlasPublishAt>=100){working.atlas=atlasEdit;atlasEdit.reset();atlasPublishAt=GetTickCount64();}scene.store(std::make_shared<Scene>(working),std::memory_order_release);}
// ---- a scene of the game's: Steve's body in Link's place (report 79) ----
// The standing body's parts are Minecraft's own boxes: the model's pixels are 0.9375/16 of a block, the neck is 24
// of them up, the hips 12, an arm turns about a point 5 out and 22 up, a leg about one 1.9 out and 12 up.
// One part: a vertex v of the standing body goes to at + shrink*R*(v - pivot), in blocks from the actor's place.
struct PartPose{float r[9]{1,0,0,0,1,0,0,0,1},pivot[3]{},at[3]{};};
namespace posing {
static void column(const float* m,int c,float out[3]){float l=0;for(int i=0;i<3;i++){out[i]=m[i*4+c];l+=out[i]*out[i];}l=1/std::sqrt(l);for(int i=0;i<3;i++)out[i]*=l;}
static void axes(const float x[3],const float y[3],const float z[3],float r[9]){for(int i=0;i<3;i++){r[i*3]=x[i];r[i*3+1]=y[i];r[i*3+2]=z[i];}}
static void apply(const float r[9],const float v[3],float o[3]){for(int i=0;i<3;i++)o[i]=r[i*3]*v[0]+r[i*3+1]*v[1]+r[i*3+2]*v[2];}
static void back(const float r[9],const float v[3],float o[3]){for(int i=0;i<3;i++)o[i]=r[i]*v[0]+r[3+i]*v[1]+r[6+i]*v[2];}
static void mul(const float a[9],const float b[9],float o[9]){for(int i=0;i<3;i++)for(int j=0;j<3;j++)o[i*3+j]=a[i*3]*b[j]+a[i*3+1]*b[3+j]+a[i*3+2]*b[6+j];}
static bool towards(const float* from,const float* to,float d[3]){float l=0;for(int i=0;i<3;i++){d[i]=to[i*4+3]-from[i*4+3];l+=d[i]*d[i];}if(!(l>1e-6f))return false;l=1/std::sqrt(l);for(int i=0;i<3;i++)d[i]*=l;return true;}
// A stiff limb hanging from \"base\" that points along d: Minecraft's own two turns, forward or back all the way round
// (about x), then out to the side (about z), and none about its own length, which a box would show as a twist.
static void limb(const float base[9],const float d[3],float out[9],float degrees[2]){
    float l[3];back(base,d,l);float side=std::asin(std::clamp(l[0],-1.f,1.f)),swing=std::cos(side)>.05f?std::atan2(-l[2],-l[1]):0;
    float ca=std::cos(swing),sa=std::sin(swing),cg=std::cos(side),sg=std::sin(side);
    const float m[9]={cg,-sg,0, ca*sg,ca*cg,-sa, sa*sg,sa*cg,ca};mul(base,m,out);degrees[0]=swing*57.29578f;degrees[1]=side*57.29578f;
}
}
// Link's bones to Steve's six parts (index: Minecraft's part, 1 head, 2 body, 3 right arm, 4 left arm, 5 right leg,
// 6 left leg; Steve's limbs stay stiff, as in vanilla Minecraft):
//  - the body stands on Skl_Root's place, turned as Spine_2 is; the head sits on the body's neck, turned as Head is;
//  - an arm hangs from the body's own shoulder and points from Link's shoulder (Arm_1) to his wrist;
//  - a leg hangs from the hips (turned as the line of Link's two hip joints is) and points from his hip (Leg_1) to his ankle;
//  - the height: Steve's proportions are not Link's, so the whole is put as high as the leg needs whose foot would
//    otherwise be lowest against Link's own (his ankle, and \"ankle\" further along the leg, is the sole).
// False without a usable skeleton (link_bones.h). what: for the log.
// Where Link's wrists and hips were at the last pose made, for the slate's rule (scenePose).
struct Hands{float wrist[2][3]{},middle[3]{},hips[9]{};bool known=false;};static Hands hands;
static bool bonesPose(float shrink,PartPose parts[7],float what[17]){
    using namespace posing;using namespace linkBones;
    hands.known=false;Pose b;if(!read(b))return false;
    const float scale=float(nativeRegions::scale),g=shrink*scale,u=.05859375f,length=12*u,ankle=.117f;   // Link's ankle bone stands 0.117 above his sole (route-307)
    float x[3],y[3],z[3],hips[9],chest[9],head[9];
    // The hips: left is from Link's right hip joint to his left one, up is towards the chest as far as that is square
    // to it. Skl_Root's own turn is not the pelvis's in every animation: where Link stands up beside the bed in the
    // game's opening, Steve's legs stood one behind the other. hips_turn in the log is the
    // angle between the two.
    {float rootX[3],spine[3],l=0,d=0,same=0;column(b.m[bRoot],0,rootX);
        for(int i=0;i<3;i++){x[i]=b.m[bLegL][i*4+3]-b.m[bLegR][i*4+3];l+=x[i]*x[i];}
        if(!(l>1e-6f)){why=810;return false;}
        l=1/std::sqrt(l);for(int i=0;i<3;i++){x[i]*=l;spine[i]=b.m[bSpine][i*4+3]-(b.m[bLegL][i*4+3]+b.m[bLegR][i*4+3])/2;d+=spine[i]*x[i];same+=rootX[i]*x[i];}
        l=0;for(int i=0;i<3;i++){y[i]=spine[i]-d*x[i];l+=y[i]*y[i];}
        if(!(l>1e-6f)){why=811;return false;}
        l=1/std::sqrt(l);for(int i=0;i<3;i++)y[i]*=l;
        z[0]=x[1]*y[2]-x[2]*y[1];z[1]=x[2]*y[0]-x[0]*y[2];z[2]=x[0]*y[1]-x[1]*y[0];axes(x,y,z,hips);
        what[16]=std::acos(std::clamp(same,-1.f,1.f))*57.29578f;}
    column(b.m[bSpine],2,x);column(b.m[bSpine],0,y);column(b.m[bSpine],1,z);axes(x,y,z,chest);
    column(b.m[bHead],2,x);column(b.m[bHead],0,y);column(b.m[bHead],1,z);axes(x,y,z,head);
    const float actor[3]={b.actor[3],b.actor[7],b.actor[11]};
    // Side 0 is Steve's left (+x), 1 his right.
    const int legBone[2]={bLegL,bLegR},ankleBone[2]={bAnkleL,bAnkleR},armBone[2]={bArmL,bArmR},wristBone[2]={bWristL,bWristR},legPart[2]={6,5},armPart[2]={4,3};
    float legWay[2][3],hipOff[2][3],pelvis[3]={b.m[bRoot][3],1e9f,b.m[bRoot][11]};
    for(int side=0;side<2;side++){
        if(!towards(b.m[legBone[side]],b.m[ankleBone[side]],legWay[side])){why=800+side;return false;}
        const float off[3]={side?-1.9f*u:1.9f*u,0,0};apply(hips,off,hipOff[side]);
        pelvis[1]=std::min(pelvis[1],b.m[ankleBone[side]][7]+ankle*legWay[side][1]-g*(hipOff[side][1]+length*legWay[side][1]));
        what[side]=b.m[ankleBone[side]][7]-actor[1];
    }
    what[2]=pelvis[1]-actor[1];what[3]=b.m[bRoot][7]-actor[1];what[15]=std::hypot(b.m[bRoot][3]-actor[0],b.m[bRoot][11]-actor[2]);
    auto place=[&](PartPose& part,const float r[9],const float pivot[3],const float world[3]){std::copy_n(r,9,part.r);for(int i=0;i<3;i++){part.pivot[i]=pivot[i];part.at[i]=(world[i]-actor[i])/scale;}};
    const float hip[3]={0,12*u,0},neck[3]={0,24*u,0},up[3]={0,12*u,0};
    place(parts[2],chest,hip,pelvis);place(parts[0],chest,hip,pelvis);
    float rise[3],neckAt[3];apply(chest,up,rise);for(int i=0;i<3;i++)neckAt[i]=pelvis[i]+g*rise[i];
    place(parts[1],head,neck,neckAt);
    for(int side=0;side<2;side++){
        float turn[9],world[3],way[3],off[3];
        const float legPivot[3]={side?-1.9f*u:1.9f*u,12*u,0};
        limb(hips,legWay[side],turn,what+4+side*2);for(int i=0;i<3;i++)world[i]=pelvis[i]+g*hipOff[side][i];
        place(parts[legPart[side]],turn,legPivot,world);
        if(!towards(b.m[armBone[side]],b.m[wristBone[side]],way)){why=900+side;return false;}
        const float armPivot[3]={side?-5*u:5*u,22*u,0},out[3]={armPivot[0],10*u,0};
        limb(chest,way,turn,what+8+side*2);apply(chest,out,off);for(int i=0;i<3;i++)world[i]=pelvis[i]+g*off[i];
        place(parts[armPart[side]],turn,armPivot,world);
    }
    for(int i=0;i<3;i++)what[12+i]=head[i*3+2];
    for(int i=0;i<3;i++){hands.wrist[0][i]=b.m[bWristL][i*4+3];hands.wrist[1][i]=b.m[bWristR][i*4+3];hands.middle[i]=(b.m[bLegL][i*4+3]+b.m[bLegR][i*4+3])/2;}
    std::copy_n(hips,9,hands.hips);hands.known=true;
    return true;
}
// The standing body, posed, takes the avatar's place and is made as tall as Link (shrunk
// to Link's height). The IPC worker, at every poll; rebuilt at most every 15 ms. lab/scene-height.txt: Link's height
// in the game's units (1.7). lab/scene-bones.txt 0: the body only stands, turned as Link's actor is.
static void scenePose(){
    static uint64_t built=0,read=0;static float height=1.7f;static bool follow=true;uint64_t now=GetTickCount64();
    if(!scenePresent.load()){sceneSlateHeld=false;if(sceneBody){sceneBody=false;working.avatar.reset();working.version=++serial;publish();}return;}
    if(!standingBody){static uint64_t told=0;if(now-told>=1000){told=now;event("{\"event\":\"scene_body\",\"vertices\":0,\"refused\":\"no_standing_body\"}");}return;}
    if(now-built<15)return;
    if(now-read>=1000){read=now;float h=1.7f;std::ifstream(directory+"/scene-height.txt")>>h;height=std::isfinite(h)?std::clamp(h,.5f,4.f):1.7f;int on=1;std::ifstream(directory+"/scene-bones.txt")>>on;follow=on!=0;}
    // What stops the body from being drawn is said once a second (route-311: the opening scene had no body and no line).
    static uint64_t refusedAt=0;auto refuse=[&](const char* why,int code){sceneSlateHeld=false;if(now-refusedAt>=1000){refusedAt=now;event("{\"event\":\"scene_body\",\"vertices\":0,\"refused\":\"%s\",\"why\":%d,\"actor\":%u,\"identity\":%u,\"memory\":%d}",why,code,body::player.load(),body::identityActor.load(),int(body::base!=nullptr));}};
    uint32_t link=body::player.load();if(!body::readable(link+0x1F8,0x30)){refuse("no_actor",0);return;}
    const float shrink=height/(1.8f*float(nativeRegions::scale));
    PartPose parts[7];float what[17]{};bool bones=follow&&bonesPose(shrink,parts,what);
    if(!bones){
        float r[9];bool fine=true;for(int row=0;row<3;row++)for(int col=0;col<3;col++){r[row*3+col]=body::f32(link+0x1F8+row*16+col*4);fine&=std::isfinite(r[row*3+col])&&std::abs(r[row*3+col])<=1.01f;}
        if(!fine){refuse("actor_matrix",follow?linkBones::why:-1);return;}
        for(auto& part:parts){part={};std::copy_n(r,9,part.r);}
    }
    built=now;
    auto b=std::make_shared<Bytes>();b->version=++serial;b->batches=standingBody->batches;b->data=standingBody->data;
    size_t count=b->data.size()/sizeof(p::RenVertex);
    for(auto& batch:b->batches){
        if(uint64_t(batch.first)+batch.count>count)continue;
        const PartPose& part=parts[std::min<uint32_t>((batch.flags>>8)&15,6)];
        for(size_t i=batch.first;i<size_t(batch.first)+batch.count;i++){
            float v[3],o[3];memcpy(v,b->data.data()+i*sizeof(p::RenVertex),12);for(int k=0;k<3;k++)v[k]-=part.pivot[k];
            posing::apply(part.r,v,o);for(int k=0;k<3;k++)o[k]=part.at[k]+shrink*o[k];
            memcpy(b->data.data()+i*sizeof(p::RenVertex),o,12);
        }
    }
    {static uint64_t told=0;if(now-told>=1000){told=now;unsigned known=0;for(auto& batch:b->batches)known+=!batch.texture||working.textures.count(batch.texture);
        for(float& f:what)if(!std::isfinite(f))f=0;
        event("{\"event\":\"scene_body\",\"vertices\":%u,\"batches\":%u,\"textures_known\":%u,\"shrink\":%.4g,\"bones\":%d,\"why\":%d,\"ankles_up\":[%.3f,%.3f],\"pelvis_up\":%.3f,\"root_up\":%.3f,\"leg_l\":[%.0f,%.0f],\"leg_r\":[%.0f,%.0f],\"arm_l\":[%.0f,%.0f],\"arm_r\":[%.0f,%.0f],\"head_forward\":[%.3f,%.3f,%.3f],\"root_aside\":%.3f,\"hips_turn\":%.0f}",
            unsigned(count),unsigned(b->batches.size()),known,shrink,int(bones),follow?linkBones::why:-1,what[0],what[1],what[2],what[3],what[4],what[5],what[6],what[7],what[8],what[9],what[10],what[11],what[12],what[13],what[14],what[15],what[16]);}}
    working.avatar=b;sceneBody=true;working.version=++serial;publish();
    // The Sheikah Slate (live_target.h sceneSlate). Worn, it hangs at Link's left hip, which is inside Steve's wider
    // body; in a hand it is what the scene is about. Measured, route-327: worn, its place in the hips' frame (from the
    // middle of the hip joints) is [0.213,-0.037,0.063], 0.006 from that in another pose, and the hanging left wrist is
    // only 0.115 to 0.151 from it, so a wrist alone says nothing; in the left hand it is 0.159 to 0.764 from the worn
    // place (0.159: the hand hanging at Link's side with the slate in it). "In a hand": farther than slateOff from the
    // worn place with a wrist at it, and from then until it is back within slateBack (0.087 is passed on the way down).
    {static const float worn[3]{.213f,-.037f,.063f};const float slateOff=.08f,slateBack=.04f;static bool held=false;
        float at[3]{},gap[2]{9,9},local[3]{},off=0;uint32_t slate=bones?playerRender::linkActor(link+0x1D9C+5*12):0;bool have=slate&&hands.known&&body::readable(slate+0x1F8,0x30);
        if(have){
            float d[3];for(int i=0;i<3;i++){at[i]=body::f32(slate+0x1F8+i*16+12);d[i]=at[i]-hands.middle[i];have&=std::isfinite(at[i]);}
            if(have){
                posing::back(hands.hips,d,local);
                for(int side=0;side<2;side++){float l=0;for(int i=0;i<3;i++){float v=at[i]-hands.wrist[side][i];l+=v*v;}gap[side]=std::sqrt(l);}
                float l=0;for(int i=0;i<3;i++){float v=local[i]-worn[i];l+=v*v;}off=std::sqrt(l);
            }
        }
        held=have&&std::min(gap[0],gap[1])<.25f&&off>(held?slateBack:slateOff);
        sceneSlateHeld=held;
        static uint64_t told=0;if(have&&now-told>=250){told=now;event("{\"event\":\"scene_slate\",\"held\":%d,\"wrist_l\":%.3f,\"wrist_r\":%.3f,\"in_hips\":[%.3f,%.3f,%.3f],\"from_worn\":%.3f,\"mode\":%d}",int(held),gap[0],gap[1],local[0],local[1],local[2],off,sceneSlate.load());}
    }
}
static void overlay(uint64_t session,uint64_t frame,uint32_t w,uint32_t h,const uint8_t* data){
    auto out=std::make_shared<Hud>();out->session=session;out->frame=frame;out->w=w;out->h=h;out->texture.resize(8+size_t(w)*h*4);
    memcpy(out->texture.data(),&w,4);memcpy(out->texture.data()+4,&h,4);
    // FrameExporter readback has GL bottom-up rows; retained Overlay.cpp inverts V.
    for(uint32_t y=0;y<h;y++)memcpy(out->texture.data()+8+size_t(y)*w*4,data+size_t(h-1-y)*w*4,size_t(w)*4);
    out->hash=hash(out->texture.data()+8,out->texture.size()-8);hud.store(out,std::memory_order_release);
}

// ---- per-frame geometry for the world-entity table (WorldRender.cpp, ported) ----------------
namespace build {
static std::vector<p::RenVertex> rows;
static void quad(const float q[4][3],const float uv[4],uint32_t color=0xFFFFFFFFu,uint32_t flags=kCutout){
    const float t[4][2]={{uv[0],uv[1]},{uv[2],uv[1]},{uv[2],uv[3]},{uv[0],uv[3]}};
    for(int k:{0,1,2,0,2,3})rows.push_back({q[k][0],q[k][1],q[k][2],t[k][0],t[k][1],color,kFullSky,flags});
}
// A brightness times a tint, as a vertex colour.
static uint32_t shade(float s,uint32_t tint=0){
    float r=s,g=s,b=s;if(tint){r*=float(tint&255)/255;g*=float((tint>>8)&255)/255;b*=float((tint>>16)&255)/255;}
    return 0xFF000000u|(uint32_t(b*255)<<16)|(uint32_t(g*255)<<8)|uint32_t(r*255);
}
// An axis-aligned box turned `yaw` about its vertical centre line: sides, top, bottom.
static void box(const float lo[3],const float size[3],float yaw,const float* side,const float* top,const float* bottom,uint32_t topTint,uint32_t flags,bool shaded){
    const float cx=lo[0]+size[0]*.5f,cz=lo[2]+size[2]*.5f,c=std::cos(yaw),s=std::sin(yaw);
    auto corner=[&](int i,float out[3]){const float lx=((i&1)?.5f:-.5f)*size[0],lz=((i&4)?.5f:-.5f)*size[2];out[0]=cx+lx*c-lz*s;out[1]=lo[1]+((i&2)?size[1]:0);out[2]=cz+lx*s+lz*c;};
    // corner bits: 1 = +x, 2 = +y, 4 = +z; each face TL, TR, BR, BL seen from outside
    static constexpr int faces[6][4]={{6,7,5,4},{3,2,0,1},{7,3,1,5},{2,6,4,0},{2,3,7,6},{4,5,1,0}};
    for(int f=0;f<6;f++){float q[4][3];for(int k=0;k<4;k++)corner(faces[f][k],q[k]);
        quad(q,f==4?top:f==5?bottom:side,shaded?shade(1,f==4?topTint:0):0xFFFFFFFFu,flags|kNormalFromFaces);}
}
// Minecraft's arrow model is 0.9 blocks long with fins 0.22 wide. SkyCraft
// shrinks it to 0.55 next to Skyrim's people; here Steve is the measure, so
// it keeps Minecraft's size.
constexpr float kArrowScale=1;
// Minecraft's arrow (or a trident) at a position, flying along d (unit length).
static void arrow(float px,float py,float pz,const float d[3],const float* uvSide,const float* uvBack,bool trident){
    float s[3]={d[2],0,-d[0]},sl=std::sqrt(s[0]*s[0]+s[2]*s[2]);if(sl<1e-3f){s[0]=1;s[2]=0;sl=1;}s[0]/=sl;s[2]/=sl;
    const float u[3]={s[1]*d[2]-s[2]*d[1],s[2]*d[0]-s[0]*d[2],s[0]*d[1]-s[1]*d[0]};constexpr float r=.70710678f;
    const float fins[2][3]={{(u[0]+s[0])*r,(u[1]+s[1])*r,(u[2]+s[2])*r},{(u[0]-s[0])*r,(u[1]-s[1])*r,(u[2]-s[2])*r}};
    auto at=[&](float along,const float* a,float sa,const float* b,float sb,float out[3]){for(int k=0;k<3;k++)out[k]=(k==0?px:k==1?py:pz)+d[k]*along+a[k]*sa+(b?b[k]*sb:0);};
    if(!trident){
        // ArrowModel (1/16 block units, scaled 0.9): two fins 16 long and 4 wide from -12 to +4, a 4x4 back plate at -11.
        constexpr float k=.9f/16*kArrowScale;
        for(const auto& f:fins){float q[4][3];at(-12*k,f,-2*k,nullptr,0,q[0]);at(4*k,f,-2*k,nullptr,0,q[1]);at(4*k,f,2*k,nullptr,0,q[2]);at(-12*k,f,2*k,nullptr,0,q[3]);quad(q,uvSide,0xFFFFFFFFu,kCutout|kNoMip);}
        float q[4][3];at(-11*k,fins[0],-2*k,fins[1],-2*k,q[0]);at(-11*k,fins[0],2*k,fins[1],-2*k,q[1]);at(-11*k,fins[0],2*k,fins[1],2*k,q[2]);at(-11*k,fins[0],-2*k,fins[1],2*k,q[3]);quad(q,uvBack,0xFFFFFFFFu,kCutout|kNoMip);
    }else{
        // Tridents: the item icon, whose diagonal runs handle (bottom-left) to tip (top-right).
        constexpr float h=.9f;
        for(const auto& f:fins){float q[4][3];at(0,f,h,nullptr,0,q[0]);at(h,f,0,nullptr,0,q[1]);at(0,f,-h,nullptr,0,q[2]);at(-h,f,0,nullptr,0,q[3]);quad(q,uvSide,0xFFFFFFFFu,kCutout|kNoMip);}
    }
}
// One edge of the outline as a screen-space quad: each row carries its own end
// and the vector to the other end (u, v and the light word hold x, y and z);
// mesh.vert widens it by a fixed number of pixels.
static void line(const float a[3],const float b[3],uint32_t color){
    auto row=[&](const float from[3],const float to[3],bool flip){
        p::RenVertex v{from[0],from[1],from[2],to[0]-from[0],to[1]-from[1],color,0,kUntextured|kTranslucent|kLine|(flip?kLineFlip:0)};
        float dz=to[2]-from[2];memcpy(&v.light,&dz,4);rows.push_back(v);
    };
    // Seen from a to b: left is "not flipped" at a and flipped at b.
    row(a,b,false);row(a,b,true);row(b,a,false);row(a,b,false);row(b,a,false);row(b,a,true);
}
}
// The table as geometry. Positions are relative to the integer origin; cracks
// are blended and the outline is lines, so each is its own group after the
// solid things. Shadows under mobs (kWeShadow) are not geometry. True when
// the geometry differs from what is published (the table's sequence moves
// with every Minecraft frame; a GPU generation is only worth a real change).
static bool dynamic(const p::WorldEntities& table){
    constexpr float kPi=3.14159265f;
    auto out=std::make_shared<Dynamic>();out->version=++serial;std::vector<p::RenVertex> cracks;build::rows.clear();
    bool any=table.hasSelection;float first[3]{};
    if(table.hasSelection)std::copy_n(table.selMin,3,first);
    for(uint32_t i=0;i<table.count&&!any;i++)if(table.entities[i].kind!=p::kWeShadow){any=true;first[0]=table.entities[i].x;first[1]=table.entities[i].y;first[2]=table.entities[i].z;}
    if(!any){if(!working.dynamic)return false;working.dynamic.reset();working.version=++serial;return true;}
    for(int k=0;k<3;k++)out->origin[k]=std::floor(first[k]);
    for(uint32_t i=0;i<table.count;i++){
        const auto& e=table.entities[i];const float px=float(e.x-out->origin[0]),py=float(e.y-out->origin[1]),pz=float(e.z-out->origin[2]);
        if(e.kind==p::kWeBlock){
            // A dropped block: a small cube spinning about its centre, like in Minecraft.
            const float s=e.scale,lo[3]={px-s*.5f,py-s*.5f,pz-s*.5f},size[3]={s,s,s};
            build::box(lo,size,e.yaw*kPi/180,e.uv[0],e.uv[1],e.uv[2],e.tint,kCutout|kNoMip,true);
        }else if(e.kind==p::kWeCrack){
            const float lo[3]={px,py,pz};auto before=build::rows.size();
            build::box(lo,e.ext,0,e.uv[0],e.uv[0],e.uv[0],0,kTranslucent|kNoMip,false);
            cracks.insert(cracks.end(),build::rows.begin()+ptrdiff_t(before),build::rows.end());build::rows.resize(before);
        }else if(e.kind==p::kWeArrow||e.kind==p::kWeTrident){
            // Minecraft arrows face (sin yaw, sin pitch, cos yaw).
            const float yaw=e.yaw*kPi/180,pitch=e.pitch*kPi/180,d[3]={std::sin(yaw)*std::cos(pitch),std::sin(pitch),std::cos(yaw)*std::cos(pitch)};
            build::arrow(px,py,pz,d,e.uv[0],e.uv[1],e.kind==p::kWeTrident);
        }else if(e.kind==p::kWeItem){
            // A flat sprite turning about the vertical, like a dropped item.
            const float spin=e.yaw*kPi/180,half=e.scale*.5f,rx=std::cos(spin)*half,rz=std::sin(spin)*half;
            const float q[4][3]={{px-rx,py+half,pz-rz},{px+rx,py+half,pz+rz},{px+rx,py-half,pz+rz},{px-rx,py-half,pz-rz}};
            build::quad(q,e.uv[0],0xFFFFFFFFu,kCutout|kNoMip);
        }
    }
    out->solid=uint32_t(build::rows.size());out->cracks=uint32_t(cracks.size());build::rows.insert(build::rows.end(),cracks.begin(),cracks.end());
    if(table.hasSelection){
        constexpr float g=.002f;float lo[3],hi[3];for(int k=0;k<3;k++){lo[k]=float(table.selMin[k]-out->origin[k])-g;hi[k]=float(table.selMax[k]-out->origin[k])+g;}
        auto corner=[&](int i,float c[3]){c[0]=(i&1)?hi[0]:lo[0];c[1]=(i&2)?hi[1]:lo[1];c[2]=(i&4)?hi[2]:lo[2];};
        static constexpr int edges[12][2]={{0,1},{2,3},{4,5},{6,7},{0,2},{1,3},{4,6},{5,7},{0,4},{1,5},{2,6},{3,7}};
        constexpr uint32_t kOutline=0x66000000u; // black, 40 % (Minecraft's block outline)
        for(const auto& edge:edges){float a[3],b[3];corner(edge[0],a);corner(edge[1],b);build::line(a,b,kOutline);}
    }
    out->outline=uint32_t(build::rows.size())-out->solid-out->cracks;
    out->data.resize(build::rows.size()*sizeof(p::RenVertex));if(!build::rows.empty())memcpy(out->data.data(),build::rows.data(),out->data.size());
    if(auto& old=working.dynamic;old&&old->solid==out->solid&&old->cracks==out->cracks&&old->outline==out->outline&&old->data==out->data&&std::equal(old->origin,old->origin+3,out->origin))return false;
    working.dynamic=out;working.version=++serial;return true;
}
}
