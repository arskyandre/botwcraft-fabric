// SAT and plane-guided voxelization adapted from SkyCraft Collision.cpp,
// bfcaf178524b92c2cdeb88e4ce0f13ef9ded6f32 (MIT; LICENSE-SkyCraft).
#pragma once
#include <array>
#include <vector>
#include <algorithm>
#include <cmath>
namespace nativeGeometry {
using Tri=botwcraft::proto::ColTri;
using Block=botwcraft::proto::ColBlock;
inline void sub(const float* a,const float* b,float* o){for(int k=0;k<3;k++)o[k]=a[k]-b[k];}
inline void cross(const float* a,const float* b,float* o){for(int k=0;k<3;k++)o[k]=a[(k+1)%3]*b[(k+2)%3]-a[(k+2)%3]*b[(k+1)%3];}
inline float dot(const float* a,const float* b){return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
inline bool valid(const Tri& t){
    for(float v:t.v)if(!std::isfinite(v)||std::abs(v)>100000)return false;
    float a[3],b[3],n[3];sub(t.v+3,t.v,a);sub(t.v+6,t.v,b);cross(a,b,n);
    return std::isfinite(dot(n,n))&&dot(n,n)>1e-12f;
}
// Rectangular box SAT, also used to remove conservative native query candidates.
inline bool overlaps(const Tri& t,const float* lo,const float* hi){
    float v[3][3],h[3];
    for(int k=0;k<3;k++){
        h[k]=(hi[k]-lo[k])*.5f;
        for(int j=0;j<3;j++)v[j][k]=t.v[j*3+k]-(hi[k]+lo[k])*.5f;
        if(std::min({v[0][k],v[1][k],v[2][k]})>h[k]||std::max({v[0][k],v[1][k],v[2][k]}) < -h[k])return false;
    }
    auto axisTest=[&](const float* axis){
        float a=dot(v[0],axis),b=dot(v[1],axis),c=dot(v[2],axis);
        float r=h[0]*std::abs(axis[0])+h[1]*std::abs(axis[1])+h[2]*std::abs(axis[2]);
        return std::min({a,b,c})<=r&&std::max({a,b,c})>=-r;
    };
    float e[3][3],n[3];for(int j=0;j<3;j++)sub(v[(j+1)%3],v[j],e[j]);
    cross(e[0],e[1],n);if(!axisTest(n))return false;
    for(auto& edge:e)for(int k=0;k<3;k++){
        float unit[3]{},axis[3];unit[k]=1;cross(edge,unit,axis);if(!axisTest(axis))return false;
    }
    return true;
}
// One bounded worker job. Failure discards all output, never publishes a partial
// occupancy region as complete. Precision triangles retain their original winding.
inline bool voxelize(const std::array<int,3>& key,const std::vector<Tri>& tris,std::vector<Block>& blocks){
    constexpr int G=64;
    std::array<uint64_t,G*G> solid{},steep{};
    uint32_t tests=0;
    // Include touching cells on both sides of exact voxel boundaries; otherwise
    // an axis-aligned zero-thickness triangle can produce an empty interval.
    auto lower=[](float x){return std::clamp(int(std::floor(x-1e-5f)),0,G-1);};
    auto upper=[](float x){return std::clamp(int(std::ceil(x+1e-5f))-1,0,G-1);};
    for(const auto& t:tris){
        if(!valid(t))return false;
        Tri q=t;for(int j=0;j<9;j++)q.v[j]=(q.v[j]-key[j%3]*8)*8;
        float lo[3],hi[3],e1[3],e2[3],n[3];
        for(int k=0;k<3;k++){lo[k]=std::min({q.v[k],q.v[k+3],q.v[k+6]});hi[k]=std::max({q.v[k],q.v[k+3],q.v[k+6]});}
        if(hi[0]<0||hi[1]<0||hi[2]<0||lo[0]>G||lo[1]>G||lo[2]>G)continue;
        sub(q.v+3,q.v,e1);sub(q.v+6,q.v,e2);cross(e1,e2,n);
        float len=std::sqrt(dot(n,n));for(float& x:n)x/=len;
        float ny=std::abs(n[1]);auto& grid=(ny>=.643f||ny<.1f)?solid:steep;
        int dom=0;for(int k=1;k<3;k++)if(std::abs(n[k])>std::abs(n[dom]))dom=k;
        int u=(dom+1)%3,v=(dom+2)%3;float d=dot(n,q.v),r=.5f*(std::abs(n[0])+std::abs(n[1])+std::abs(n[2]));
        for(int iu=lower(lo[u]);iu<=upper(hi[u]);iu++)for(int iv=lower(lo[v]);iv<=upper(hi[v]);iv++){
            float s0=(d-r-n[u]*(iu+.5f)-n[v]*(iv+.5f))/n[dom];
            float s1=(d+r-n[u]*(iu+.5f)-n[v]*(iv+.5f))/n[dom];
            int a0=std::max(lower(lo[dom]),int(std::floor(std::min(s0,s1)-.5f)));
            int a1=std::min(upper(hi[dom]),int(std::ceil(std::max(s0,s1)-.5f)));
            for(int id=a0;id<=a1;id++){
                if(++tests>2000000)return false;
                int p[3];p[dom]=id;p[u]=iu;p[v]=iv;
                float boxLo[3],boxHi[3];for(int k=0;k<3;k++){boxLo[k]=float(p[k]);boxHi[k]=boxLo[k]+1;}
                if(overlaps(q,boxLo,boxHi))grid[p[1]*G+p[2]]|=1ull<<p[0];
            }
        }
    }
    // Retain SkyCraft's steep-surface voxel policy; no stair helper is invented.
    for(int by=0;by<8;by++)for(int bz=0;bz<8;bz++)for(int bx=0;bx<8;bx++){
        uint64_t mask=0xffull<<(bx*8);int minY=64,maxY=-1;
        for(int y=by*8;y<by*8+8;y++)for(int z=bz*8;z<bz*8+8;z++)if(steep[y*G+z]&mask){minY=std::min(minY,y);maxY=std::max(maxY,y);}
        for(int y=minY;y<=maxY;y++)for(int z=bz*8;z<bz*8+8;z++)solid[y*G+z]|=mask;
    }
    blocks.clear();
    for(int by=0;by<8;by++)for(int bz=0;bz<8;bz++)for(int bx=0;bx<8;bx++){
        Block b{};bool any=false;
        for(int y=0;y<8;y++)for(int z=0;z<8;z++){b.bits[y]|=((solid[(by*8+y)*G+bz*8+z]>>(bx*8))&0xffull)<<(z*8);any|=b.bits[y]!=0;}
        if(any){b.x=key[0]*8+bx;b.y=key[1]*8+by;b.z=key[2]*8+bz;blocks.push_back(b);}
    }
    return true;
}
}
