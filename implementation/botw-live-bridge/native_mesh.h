// Direct decoder for US208 Havok static collision. Worker-only and pure: it
// reads an owned copy of guest bytes (never a guest pointer) and produces the
// triangles BOTW's own query would return, keyed like hkpShapeKey.
//
// Layouts (Havok 2012, 32-bit big-endian), established against the game's own
// triangles by native_mesh.py on live captures (route-26: 2553 of 2553 keyed
// triangles within 7.3e-5 native units; region lookup without a missing key):
//  hkpBvCompressedMeshShape (vtable 1040D2E8), hkcdDefaultStaticMeshTree at +50:
//    +60/+70 domain min/max; arrays as (pointer, count): +8C sections (0x60
//    bytes), +98 primitives (4 vertex indices), +A4 shared vertex indices
//    (u16), +B0 packed vertices (u32, 11:11:10), +BC shared vertices (u64,
//    21:21:22 over the domain).
//    Section: +10/+20 bounds, +30 origin, +3C scale, +48 first packed vertex,
//    +4C shared (first<<8 | count), +50 primitives (first<<8 | count), +58
//    packed vertex count. Key = section<<8 | primitive<<1 | second triangle.
//    A primitive is a triangle (c==d) or a quad (a,b,c) + (a,c,d).
//  hkpStaticCompoundShape (vtable 1040DA74): +18 key bits per child, +1C child
//    key mask, +20/+24 instances (0x40 bytes): translation (w: flag bits),
//    rotation quaternion, scale, +30 child shape. Flags 1 leaf, 2 transform,
//    4 scale, 0x10 disabled. Key = instance<<bits | child key.
// Anything outside these forms makes the root undecodable (false), and the
// caller keeps reading it through the game.
//
// Collision filters (route-198/200 dumps of the Eastern Abbey's compounds, 4680 children, 333 meshes):
//  the game asks its group filter about each child, not only about the body. A compound's instance holds
//  +34 its filter and +38 the mask of the bits taken from the child's own; a child's filter is that of the
//  instance or-ed with the masked one of the child: convex children have mask 0 and their own filter, meshes
//  filter 0 and mask FFFFFFFF. A mesh has (+1D set) a filter for each primitive: +20/+24 a palette of
//  filters, +C8/+CC runs of 8 bytes (a word whose low byte is the place in the palette, then the first
//  primitive of the run in its section and how many), a section's runs at its +54 (first<<8 | count).
//  The palettes hold walls of the game's that are not for everyone: 90FFFD00 only for animals (the box in a
//  shrine's doorway, which stopped Steve at the lift), 90F7FF00, 90200100, and so on (physDefines.h of the
//  decompilation: bit 31, the layer in bits 25-29, in bits 8-23 the kinds of thing that pass).
#pragma once
#include <cstdint>
#include <cstring>
#include <cmath>
#include <vector>
#include <map>
#include <algorithm>
namespace nativeMesh {
constexpr uint32_t kMesh=0x1040D2E8,kCompound=0x1040DA74,kMaxInstances=8192;
struct Keyed {uint32_t key=0;float v[9]{};};
static uint32_t be32(const uint8_t* p){uint32_t v;memcpy(&v,p,4);return __builtin_bswap32(v);}
static float bef(const uint8_t* p){uint32_t v=be32(p);float f;memcpy(&f,&v,4);return f;}
struct Image {
    std::vector<std::pair<uint32_t,std::vector<uint8_t>>> ranges;uint64_t bytes=0;
    const uint8_t* at(uint32_t address,uint64_t size)const{
        for(const auto& [start,blob]:ranges)if(address>=start&&uint64_t(address)+size<=uint64_t(start)+blob.size())return blob.data()+(address-start);
        return nullptr;
    }
};
struct Mesh {
    const uint8_t *sections=nullptr,*primitives=nullptr,*sharedIndex=nullptr,*packed=nullptr,*shared=nullptr;
    uint32_t sectionCount=0,primitiveCount=0,sharedIndexCount=0,packedCount=0,sharedCount=0;float lo[3]{},hi[3]{};
    // The filter of each primitive: its place in the palette, by its place in `primitives`. Empty when the
    // mesh has none or its runs are not as described above (then nothing is said about its triangles).
    std::vector<uint32_t> palette;std::vector<uint8_t> filterIndex;
    // The user data of each primitive, kept the same way: the game's material word (physMaterialMask) of the triangle.
    std::vector<uint32_t> users;std::vector<uint8_t> userIndex;
};
// A compound's instance (or the one mesh of a mesh root): what its children's filters are made of.
struct Child {uint32_t filter=0,mask=0;int32_t mesh=-1;};
struct Instance {uint32_t index=0,mesh=0;float t[3]{},q[4]{0,0,0,1},s[3]{1,1,1},corner[8][3]{};};
struct Root {
    Image image;uint32_t vtable=0,bits=0,mask=~0u,triangles=0;
    std::vector<Mesh> meshes;std::vector<Instance> instances;std::vector<Child> children;
};
static void place(const Instance& i,const float* v,float* out){
    // hkQsTransform: scale, rotate by the quaternion, translate.
    float p[3]{v[0]*i.s[0],v[1]*i.s[1],v[2]*i.s[2]};const float x=i.q[0],y=i.q[1],z=i.q[2],w=i.q[3];
    float c[3]{y*p[2]-z*p[1]+w*p[0],z*p[0]-x*p[2]+w*p[1],x*p[1]-y*p[0]+w*p[2]};
    out[0]=p[0]+2*(y*c[2]-z*c[1])+i.t[0];out[1]=p[1]+2*(z*c[0]-x*c[2])+i.t[1];out[2]=p[2]+2*(x*c[1]-y*c[0])+i.t[2];
}
static bool finite3(const float* v){return std::isfinite(v[0])&&std::isfinite(v[1])&&std::isfinite(v[2]);}
static bool parseMesh(const Image& image,uint32_t shape,Mesh& m){
    auto h=image.at(shape,0x100);if(!h||be32(h)!=kMesh)return false;
    for(int k=0;k<3;k++){m.lo[k]=bef(h+0x60+k*4);m.hi[k]=bef(h+0x70+k*4);}
    if(!finite3(m.lo)||!finite3(m.hi))return false;
    auto array=[&](uint32_t offset,uint32_t element,const uint8_t*& data,uint32_t& count){
        count=be32(h+offset+4);data=count?image.at(be32(h+offset),uint64_t(count)*element):nullptr;return !count||data;
    };
    return array(0x8C,0x60,m.sections,m.sectionCount)&&array(0x98,4,m.primitives,m.primitiveCount)&&array(0xA4,2,m.sharedIndex,m.sharedIndexCount)&&
        array(0xB0,4,m.packed,m.packedCount)&&array(0xBC,8,m.shared,m.sharedCount)&&m.sectionCount<=0xFFFFFF;
}
// The primitives' filters of a parsed mesh. Every section's runs must cover its primitives in order, one
// after the other, and name a place within the palette; else the mesh keeps none.
static void parseFilters(const Image& image,uint32_t shape,Mesh& m){
    m.palette.clear();m.filterIndex.clear();
    auto h=image.at(shape,0x100);if(!h||h[0x1D]!=1)return;
    const uint32_t colours=be32(h+0x24),runCount=be32(h+0xCC);if(!colours||colours>256||!runCount)return;
    auto palette=image.at(be32(h+0x20),uint64_t(colours)*4),runs=image.at(be32(h+0xC8),uint64_t(runCount)*8);if(!palette||!runs)return;
    std::vector<uint8_t> index(m.primitiveCount,0xFF);
    for(uint32_t section=0;section<m.sectionCount;section++){
        const uint8_t* s=m.sections+section*0x60;const uint32_t prims=be32(s+0x50),run=be32(s+0x54);uint32_t next=0;
        if(uint64_t(prims>>8)+(prims&0xFF)>m.primitiveCount||uint64_t(run>>8)+(run&0xFF)>runCount)return;
        for(uint32_t j=0;j<(run&0xFF);j++){
            const uint8_t* raw=runs+(uint64_t(run>>8)+j)*8;const uint32_t place=be32(raw)&0xFF,first=raw[4],count=raw[5];
            if(first!=next||!count||first+count>(prims&0xFF)||place>=colours)return;
            for(uint32_t n=0;n<count;n++)index[(prims>>8)+first+n]=uint8_t(place);
            next=first+count;
        }
        if(next!=(prims&0xFF))return;
    }
    m.palette.resize(colours);for(uint32_t n=0;n<colours;n++)m.palette[n]=be32(palette+n*4);
    m.filterIndex=std::move(index);
}
// The primitives' user data of a parsed mesh. A run's word holds the place in the palette of filters in its lowest
// byte (parseFilters) and the place in the palette of user data (+2C, count +30) in the next. Observed (the dumps
// of 2026-10-12, lab/meshes-*.csv, 333 meshes of the Great Plateau): in each of 7412 runs that byte is below the
// count of that palette. The words there are the game's material words: material in bits 0-5, sub material 6-9,
// floor code 10-14, wall code 15-19 (the decompilation's physMaterialMask.h); wall codes 0, 1, 2, 3, 4, 6, 7, 8 occur.
static void parseUsers(const Image& image,uint32_t shape,Mesh& m){
    m.users.clear();m.userIndex.clear();
    auto h=image.at(shape,0x100);if(!h)return;
    const uint32_t words=be32(h+0x30),runCount=be32(h+0xCC);if(!words||words>256||!runCount)return;
    auto palette=image.at(be32(h+0x2C),uint64_t(words)*4),runs=image.at(be32(h+0xC8),uint64_t(runCount)*8);if(!palette||!runs)return;
    std::vector<uint8_t> index(m.primitiveCount,0xFF);
    for(uint32_t section=0;section<m.sectionCount;section++){
        const uint8_t* s=m.sections+section*0x60;const uint32_t prims=be32(s+0x50),run=be32(s+0x54);uint32_t next=0;
        if(uint64_t(prims>>8)+(prims&0xFF)>m.primitiveCount||uint64_t(run>>8)+(run&0xFF)>runCount)return;
        for(uint32_t j=0;j<(run&0xFF);j++){
            const uint8_t* raw=runs+(uint64_t(run>>8)+j)*8;const uint32_t place=be32(raw)>>8&0xFF,first=raw[4],count=raw[5];
            if(first!=next||!count||first+count>(prims&0xFF)||place>=words)return;
            for(uint32_t n=0;n<count;n++)index[(prims>>8)+first+n]=uint8_t(place);
            next=first+count;
        }
        if(next!=(prims&0xFF))return;
    }
    m.users.resize(words);for(uint32_t n=0;n<words;n++)m.users[n]=be32(palette+n*4);
    m.userIndex=std::move(index);
}
// The user data (the game's material word) of the triangle a child key of the root names; false when it is not
// known here (the child is no mesh, the mesh keeps none, a key out of range).
static bool keyUser(const Root& r,uint32_t key,uint32_t& out){
    uint32_t index=0,child=key;
    if(r.vtable==kCompound){if(r.bits<1||r.bits>31)return false;index=key>>r.bits;child=key&r.mask;}
    if(index>=r.children.size())return false;
    const Child& c=r.children[index];if(c.mesh<0||size_t(c.mesh)>=r.meshes.size())return false;
    const Mesh& m=r.meshes[c.mesh];const uint32_t section=child>>8,n=(child>>1)&0x7F;
    if(m.userIndex.empty()||section>=m.sectionCount)return false;
    const uint32_t prims=be32(m.sections+section*0x60+0x50);if(n>=(prims&0xFF))return false;
    const uint8_t place=m.userIndex[(prims>>8)+n];if(place>=m.users.size())return false;
    out=m.users[place];return true;
}
// The filter the game's group filter is given for a child key of the root; false when it is not known here
// (no filters in the mesh, a child that is no mesh but takes bits of its own filter, a key out of range).
static bool keyFilter(const Root& r,uint32_t key,uint32_t& out){
    uint32_t index=0,child=key;
    if(r.vtable==kCompound){if(r.bits<1||r.bits>31)return false;index=key>>r.bits;child=key&r.mask;}
    if(index>=r.children.size())return false;
    const Child& c=r.children[index];uint32_t own=0;
    if(c.mask){
        if(c.mesh<0||size_t(c.mesh)>=r.meshes.size())return false;
        const Mesh& m=r.meshes[c.mesh];const uint32_t section=child>>8,n=(child>>1)&0x7F;
        if(m.filterIndex.empty()||section>=m.sectionCount)return false;
        const uint32_t prims=be32(m.sections+section*0x60+0x50);if(n>=(prims&0xFF))return false;
        const uint8_t place=m.filterIndex[(prims>>8)+n];if(place>=m.palette.size())return false;
        own=m.palette[place];
    }
    out=(own&c.mask)|c.filter;return true;
}
// Copies everything the decoder needs through `read(address,size,bytes)` and
// parses it. `budget` bounds the copy.
template<class Read> static bool capture(Root& r,uint32_t shape,uint32_t vtable,Read&& read,uint64_t budget=64ull<<20){
    r=Root{};r.vtable=vtable;
    auto add=[&](uint32_t address,uint64_t size){
        if(!size)return true;
        for(const auto& [start,blob]:r.image.ranges)if(start==address&&blob.size()>=size)return true;
        if(size>budget||address<0x10000000||(address&3)||uint64_t(address)+size>0xA0000000)return false;
        std::vector<uint8_t> bytes(size);if(!read(address,uint32_t(size),bytes.data()))return false;
        budget-=size;r.image.bytes+=size;r.image.ranges.emplace_back(address,std::move(bytes));return true;
    };
    auto addMesh=[&](uint32_t child){
        if(!add(child,0x100))return false;
        auto h=r.image.at(child,0x100);if(be32(h)!=kMesh)return true; // other children stay with the game
        static constexpr std::pair<uint32_t,uint32_t> arrays[]{{0x8C,0x60},{0x98,4},{0xA4,2},{0xB0,4},{0xBC,8}};
        for(auto [offset,element]:arrays){
            h=r.image.at(child,0x100); // earlier pointers move when ranges grow
            if(!add(be32(h+offset),(uint64_t(be32(h+offset+4))*element+3)&~3ull))return false;
        }
        // The palette of filters and the runs: without them the mesh still decodes, and says nothing of filters.
        static constexpr std::pair<uint32_t,uint32_t> filters[]{{0x20,4},{0xC8,8},{0x2C,4}};   // +2C: the palette of user data
        for(auto [offset,element]:filters){
            h=r.image.at(child,0x100);
            if(be32(h+offset+4)<=0x100000)add(be32(h+offset),uint64_t(be32(h+offset+4))*element);
        }
        return true;
    };
    std::map<uint32_t,uint32_t> meshOf;
    auto meshIndex=[&](uint32_t child,uint32_t& index){
        if(auto it=meshOf.find(child);it!=meshOf.end()){index=it->second;return true;}
        Mesh m;if(!parseMesh(r.image,child,m))return false;
        parseFilters(r.image,child,m);parseUsers(r.image,child,m);
        index=uint32_t(r.meshes.size());r.meshes.push_back(std::move(m));meshOf[child]=index;return true;
    };
    struct Pending {Instance i;uint32_t child;};std::vector<Pending> pending;
    if(vtable==kMesh){
        if(!addMesh(shape))return false;
        r.children.assign(1,Child{0,~0u,-1});
        pending.push_back({Instance{},shape});
    }else if(vtable==kCompound){
        if(!add(shape,0x100))return false;
        auto h=r.image.at(shape,0x100);if(be32(h)!=kCompound)return false;
        r.bits=h[0x18];r.mask=be32(h+0x1C);uint32_t data=be32(h+0x20),count=be32(h+0x24);
        if(r.bits<1||r.bits>31||r.mask!=(1u<<r.bits)-1||count>kMaxInstances||(count&&(uint64_t(count)-1)>>(32-r.bits)))return false;
        if(!add(data,uint64_t(count)*0x40))return false;
        r.children.assign(count,Child{});
        for(uint32_t n=0;n<count;n++){
            auto raw=r.image.at(data+n*0x40,0x40);uint32_t flags=be32(raw+0x0C)&0x7F,child=be32(raw+0x30);
            r.children[n].filter=be32(raw+0x34);r.children[n].mask=be32(raw+0x38);
            if(flags&0x10)continue;                       // disabled: the game returns no key for it
            if(!addMesh(child))return false;
            raw=r.image.at(data+n*0x40,0x40);
            if(be32(r.image.at(child,4))!=kMesh)continue;  // convex and other children stay with the game
            if(flags&~0x17u)return false;                 // flip or disabled keys: not established
            Instance i;i.index=n;
            for(int k=0;k<3;k++){i.t[k]=bef(raw+k*4);i.s[k]=bef(raw+0x20+k*4);}
            for(int k=0;k<4;k++)i.q[k]=bef(raw+0x10+k*4);
            if(!finite3(i.t)||!finite3(i.s)||!finite3(i.q)||!std::isfinite(i.q[3]))return false;
            pending.push_back({i,child});
        }
    }else return false;
    // Parse only after every range is in place: Mesh keeps pointers into them.
    for(auto& p:pending){
        if(!meshIndex(p.child,p.i.mesh))return false;
        if(p.i.index<r.children.size())r.children[p.i.index].mesh=int32_t(p.i.mesh);
        const auto& m=r.meshes[p.i.mesh];
        for(int c=0;c<8;c++){float v[3]{c&1?m.hi[0]:m.lo[0],c&2?m.hi[1]:m.lo[1],c&4?m.hi[2]:m.lo[2]};place(p.i,v,p.i.corner[c]);}
        r.instances.push_back(p.i);
    }
    return true;
}
// Triangles of the root whose bounds reach the world box [lo,hi] (native
// units). `xf` is the body's hkTransform as 16 floats (three rotation
// columns, translation). Exact triangle/box tests are the caller's.
static bool region(const Root& r,const float* xf,const float* lo,const float* hi,std::vector<Keyed>& out){
    auto world=[&](const float* v,float* w){for(int k=0;k<3;k++){float x=xf[12+k];for(int j=0;j<3;j++)x+=xf[j*4+k]*v[j];w[k]=x;}};
    auto reaches=[&](const float* a,const float* b,float pad){for(int k=0;k<3;k++)if(a[k]>hi[k]+pad||b[k]<lo[k]-pad)return false;return true;};
    for(const auto& i:r.instances){
        float a[3]{INFINITY,INFINITY,INFINITY},b[3]{-INFINITY,-INFINITY,-INFINITY};
        for(const auto& corner:i.corner){float w[3];world(corner,w);for(int k=0;k<3;k++){a[k]=std::min(a[k],w[k]);b[k]=std::max(b[k],w[k]);}}
        if(!reaches(a,b,1e-3f))continue;
        const auto& m=r.meshes[i.mesh];
        for(uint32_t section=0;section<m.sectionCount;section++){
            const uint8_t* s=m.sections+section*0x60;
            float slo[3],shi[3];for(int k=0;k<3;k++){slo[k]=bef(s+0x10+k*4);shi[k]=bef(s+0x20+k*4);a[k]=INFINITY;b[k]=-INFINITY;}
            for(int c=0;c<8;c++){
                float v[3]{c&1?shi[0]:slo[0],c&2?shi[1]:slo[1],c&4?shi[2]:slo[2]},p[3],w[3];place(i,v,p);world(p,w);
                for(int k=0;k<3;k++){a[k]=std::min(a[k],w[k]);b[k]=std::max(b[k],w[k]);}
            }
            if(!reaches(a,b,1e-3f))continue;
            float origin[3],scale[3];for(int k=0;k<3;k++){origin[k]=bef(s+0x30+k*4);scale[k]=bef(s+0x3C+k*4);}
            uint32_t firstPacked=be32(s+0x48),shared=be32(s+0x4C),prims=be32(s+0x50),packedCount=s[0x58];
            auto vertex=[&](uint32_t index,float* w){
                float v[3];
                if(index<packedCount){
                    if(uint64_t(firstPacked)+index>=m.packedCount)return false;
                    uint32_t p=be32(m.packed+(firstPacked+index)*4),q[3]{p&0x7FF,(p>>11)&0x7FF,p>>22};
                    for(int k=0;k<3;k++)v[k]=origin[k]+scale[k]*float(q[k]);
                }else{
                    uint64_t slot=uint64_t(shared>>8)+index-packedCount;if(slot>=m.sharedIndexCount)return false;
                    uint32_t at=uint32_t(m.sharedIndex[slot*2])<<8|m.sharedIndex[slot*2+1];if(at>=m.sharedCount)return false;
                    uint64_t p=uint64_t(be32(m.shared+at*8))<<32|be32(m.shared+at*8+4);
                    uint32_t q[3]{uint32_t(p&0x1FFFFF),uint32_t((p>>21)&0x1FFFFF),uint32_t(p>>42)};
                    static constexpr float span[3]{float(0x1FFFFF),float(0x1FFFFF),float(0x3FFFFF)};
                    for(int k=0;k<3;k++)v[k]=m.lo[k]+(m.hi[k]-m.lo[k])*(float(q[k])/span[k]);
                }
                float p[3];place(i,v,p);world(p,w);return finite3(w);
            };
            for(uint32_t n=0;n<(prims&0xFF);n++){
                if(uint64_t(prims>>8)+n>=m.primitiveCount)return false;
                const uint8_t* idx=m.primitives+((prims>>8)+n)*4;
                const bool quad=idx[2]!=idx[3];
                if(idx[0]==idx[1]||idx[1]==idx[2]||idx[0]==idx[2]||(quad&&(idx[3]==idx[0]||idx[3]==idx[1])))return false; // not a triangle or quad
                for(uint32_t second=0;second<=uint32_t(quad);second++){
                    Keyed t;uint32_t child=section<<8|n<<1|second;
                    if(r.vtable==kCompound&&(child&~r.mask))return false;
                    t.key=r.vtable==kCompound?i.index<<r.bits|child:child;
                    const uint8_t corners[3]{idx[0],second?idx[2]:idx[1],second?idx[3]:idx[2]};
                    for(int c=0;c<3;c++)if(!vertex(corners[c],t.v+c*3))return false;
                    for(int k=0;k<3;k++){a[k]=std::min({t.v[k],t.v[3+k],t.v[6+k]});b[k]=std::max({t.v[k],t.v[3+k],t.v[6+k]});}
                    if(reaches(a,b,0))out.push_back(t);
                }
            }
        }
    }
    return true;
}
// Decoded against the game's own keyed triangles for the same box. Every
// native triangle inside the box must be decoded with the same vertices; when
// the native read of the root was complete, the decoder must not produce a
// triangle inside the box that the game did not return. "Inside" leaves a
// margin, because the two sides cull with different conservative bounds.
struct Comparison {uint32_t matched=0,missing=0,moved=0,extra=0;bool ok()const{return !missing&&!moved&&!extra;}};
static Comparison compare(std::vector<Keyed>& mine,std::vector<Keyed>& native,const float* lo,const float* hi,bool complete){
    auto byKey=[](const Keyed& a,const Keyed& b){return a.key<b.key;};
    std::sort(mine.begin(),mine.end(),byKey);std::sort(native.begin(),native.end(),byKey);
    float inLo[3],inHi[3];for(int k=0;k<3;k++){inLo[k]=lo[k]+.01f;inHi[k]=hi[k]-.01f;}
    auto inside=[&](const Keyed& t){botwcraft::proto::ColTri c{};std::copy_n(t.v,9,c.v);return nativeGeometry::valid(c)&&nativeGeometry::overlaps(c,inLo,inHi);};
    Comparison r;
    for(const auto& t:native){
        auto it=std::lower_bound(mine.begin(),mine.end(),t,byKey);
        if(it==mine.end()||it->key!=t.key){if(inside(t))++r.missing;continue;}
        float worst=0;for(int k=0;k<9;k++)worst=std::max(worst,std::abs(it->v[k]-t.v[k]));
        if(worst>2e-3f)++r.moved;else ++r.matched;
    }
    if(complete)for(const auto& t:mine)if(!std::binary_search(native.begin(),native.end(),t,byKey)&&inside(t))++r.extra;
    return r;
}
}
