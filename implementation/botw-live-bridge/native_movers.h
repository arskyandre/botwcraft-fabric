// Bodies that move (report 74). Worker thread only; owned bytes, no guest pointers.
//
// The problem: the collision given to Minecraft lagged behind moving objects, and Steve was not carried by the
// game's moving platforms or the Cryonis' rising ice.
//
// Source behaviour before this: a body that is not fixed was part of the regions it lay in, like the ground. A
// region is read again every 750 to 1500 ms (native_regions.h), so Minecraft held the body where it had been up
// to that long ago, and at the place it had left until that region was read again.
//
// Now: every harvest lists every body of the world with its transform (nativeShapes::inventory), whatever region
// the harvest is for, and a harvest runs each game frame. A body of an island that is not the fixed one is kept
// here: its triangles in its own frame, taken whenever a harvested region reaches it, and its latest transform.
// It is no longer part of any region. Whenever its transform or its triangles change it goes to Minecraft whole,
// in a message of its own (kColBody, protocol 16) with the transform, and Minecraft carries what stands on it by
// the change of the transform between two of its ticks.
//
// Known limit: the 8x8x8 masks of a region (kColRegion), which Minecraft's own mobs and items collide with, no
// longer hold these bodies. Minecraft's player, its crosshair and its arrows use the triangles.
#pragma once
#include <map>
#include <unordered_map>
namespace nativeMovers {
using Tri=std::array<float,9>;           // in the body's own frame, the game's units
// Where a part's triangles came from: 0 a mesh's or compound's own, 1 convex pieces, 2 leaf boxes, 3 the box of its bounds.
constexpr uint64_t kPieces=1,kLeafBoxes=2,kBounds=3;
constexpr size_t kMost=8192;             // triangles of one body sent
constexpr uint32_t kMissed=3;            // complete lists of the world without it before it is taken as gone
constexpr float kSame=2e-3f;             // the same part read again at another place of the body differs by less (float rounding)
constexpr double kRange=48;              // Minecraft units from Minecraft's player within which a body is sent
constexpr float kStandIn=6;              // the largest side (the game's units) of bounds that may stand in for a body not read yet
struct Seen {uint32_t body=0,shape=0;float xf[16]{};};
struct Mover {
    uint32_t body=0,shape=0,missed=0,serial=0;float xf[16]{},sentXf[16]{},reach=0;
    std::map<uint64_t,std::vector<Tri>> parts;      // by source (high word) and the game's key of the child
    std::vector<botwcraft::proto::ColTri> sent;      // as Minecraft holds it now
    bool dirty=false,told=false;
};
static std::unordered_map<uint32_t,Mover> movers;
static std::vector<uint32_t> gone;       // told to Minecraft and no more: to be taken back
static std::atomic<int> mode{1};         // lab/movers.txt 0: off, bodies stay in their regions as before; 2: every message is logged
static uint64_t revision=0,published=0,goneSent=0,geometry=0;
using Gathered=std::unordered_map<uint32_t,std::map<uint64_t,std::vector<Tri>>>;
// Havok's transform: three columns of rotation (xf[j*4+k], column j, row k) and the place (xf[12..14]).
static void toLocal(const float* xf,const float* w,float* l){
    float d[3]{w[0]-xf[12],w[1]-xf[13],w[2]-xf[14]};
    for(int j=0;j<3;j++)l[j]=xf[j*4]*d[0]+xf[j*4+1]*d[1]+xf[j*4+2]*d[2];
}
static void toWorld(const float* xf,const float* l,float* w){for(int k=0;k<3;k++)w[k]=xf[12+k]+xf[k]*l[0]+xf[4+k]*l[1]+xf[8+k]*l[2];}
// A triangle of this snapshot (Minecraft's space, as the worker makes them) into the frame of the body it belongs to.
static void gather(Gathered& g,const Seen& c,uint64_t part,const botwcraft::proto::ColTri& t){
    Tri l;
    for(int v=0;v<3;v++){
        float w[3];for(int k=0;k<3;k++)w[k]=(t.v[v*3+k]-nativeRegions::origin[k])*nativeRegions::scale+nativeRegions::anchor[k];
        toLocal(c.xf,w,&l[v*3]);
    }
    g[c.body][part].push_back(l);
}
static bool same(const std::vector<Tri>& a,const std::vector<Tri>& b){
    if(a.size()!=b.size())return false;
    for(size_t i=0;i<a.size();i++)for(int k=0;k<9;k++)if(!(std::abs(a[i][k]-b[i][k])<=kSame))return false;
    return true;
}
// One snapshot: the transform of every body seen, the parts a harvested region gave of some of them, and, when
// the world's list was read whole, who is no longer in it.
static void update(const std::vector<Seen>& seen,Gathered& gathered,bool complete){
    if(complete)for(auto& [body,m]:movers)++m.missed;
    for(const auto& c:seen){
        auto& m=movers[c.body];
        if(m.body!=c.body||m.shape!=c.shape){   // new, or another body under the number of one that is gone
            if(m.told)gone.push_back(c.body);
            m=Mover{};m.body=c.body;m.shape=c.shape;
        }
        std::copy_n(c.xf,16,m.xf);m.missed=0;
        auto g=gathered.find(c.body);if(g==gathered.end())continue;
        // The box of the bounds stands in only until a harvest has given the body's own pieces or triangles.
        bool exact=false;for(const auto& [part,tris]:m.parts)exact|=part>>32!=kBounds;
        for(auto& [part,tris]:g->second){
            if(part>>32==kBounds){if(exact)continue;}
            else if(!exact){exact=true;if(!m.parts.empty()){m.parts.clear();m.dirty=true;}}
            auto& held=m.parts[part];
            if(!same(held,tris)){held=std::move(tris);m.dirty=true;++geometry;}
        }
        if(m.dirty){
            m.reach=0;
            for(const auto& [part,tris]:m.parts)for(const auto& t:tris)for(int v=0;v<3;v++)m.reach=std::max(m.reach,std::sqrt(t[v*3]*t[v*3]+t[v*3+1]*t[v*3+1]+t[v*3+2]*t[v*3+2]));
        }
    }
    if(complete)for(auto i=movers.begin();i!=movers.end();){
        if(i->second.missed>=kMissed){if(i->second.told)gone.push_back(i->first);i=movers.erase(i);}else ++i;
    }
}
// The body where it is now, in Minecraft's space.
static void minecraft(const Mover& m,std::vector<botwcraft::proto::ColTri>& out){
    out.clear();
    for(const auto& [part,tris]:m.parts)for(const auto& l:tris){
        if(out.size()>=kMost)return;
        botwcraft::proto::ColTri t{};
        for(int v=0;v<3;v++){
            float w[3];toWorld(m.xf,&l[v*3],w);
            for(int k=0;k<3;k++)t.v[v*3+k]=(w[k]-nativeRegions::anchor[k])/nativeRegions::scale+nativeRegions::origin[k];
        }
        if(nativeGeometry::valid(t))out.push_back(t);
    }
}
static void clear(){movers.clear();gone.clear();++revision;}
}
