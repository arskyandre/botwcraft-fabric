#version 450
// Minecraft's opaque and cutout geometry written into BOTW's G-buffer (report 44):
// material class and flags, albedo, view-space normal, emissive colour, and the scene depth.
// BOTW's own passes then light, shadow, fog and post-process it as its own geometry.
layout(binding=0) uniform sampler2D atlas;
// viewX, viewY: the camera's right and up in world space (rows of its view matrix).
// viewX.w: material class + 256 x flags (bytes of the R8G8 target). viewY.w: the normal
// target's alpha byte + 256 x (the floor of the sky-light factor, in 1/255).
// params.z: albedo scale (Minecraft's textures are brighter than BOTW's albedo under the same
// sun). params.w: brightness of full block light, in BOTW's HDR units.
layout(push_constant,row_major) uniform Push { mat4 viewProjection; vec4 anchor; vec4 params; vec4 viewX; vec4 viewY; } pc;
layout(location=0) in vec2 uv;
layout(location=1) in vec4 color;
layout(location=2) in vec2 levels;
layout(location=3) flat in uint bits;
layout(location=4) in vec3 model;
layout(location=0) out vec4 material;
layout(location=1) out vec4 albedo;   // an sRGB target: written linear
layout(location=2) out vec4 normal;
layout(location=3) out vec4 emissive; // the HDR scene colour, before lighting adds to it
float curve(float l){return l/(4.0-3.0*l);}
vec3 toLinear(vec3 c){return mix(c/12.92,pow((c+0.055)/1.055,vec3(2.4)),step(vec3(0.04045),c));}
void main(){
    vec4 tex=(bits&4u)!=0u?vec4(1):texture(atlas,uv);
    if(tex.a<0.5)discard;
    // The face's normal in world space (Minecraft's axes are the native world's): Direction
    // ordinal + 1 from the mod; 0 for plants, lit as if they faced up; 7 for entity models,
    // taken from the triangle and turned towards the eye.
    uint n=(bits>>4u)&7u;vec3 world=vec3(0,1,0);
    if(n==7u){vec3 f=cross(dFdy(model),dFdx(model));float l=dot(f,f);if(l>1e-20)world=f*inversesqrt(l);}
    else if(n==1u)world=vec3(0,-1,0);
    else if(n==3u)world=vec3(0,0,-1);
    else if(n==4u)world=vec3(0,0,1);
    else if(n==5u)world=vec3(-1,0,0);
    else if(n==6u)world=vec3(1,0,0);
    vec3 vx=pc.viewX.xyz,vy=pc.viewY.xyz,vz=cross(vx,vy);
    vec3 view=vec3(dot(vx,world),dot(vy,world),dot(vz,world));
    if(n==7u){
        // Towards the eye, not towards the camera's plane: off the camera's axis a side seen
        // edge-on has a normal whose view z is around zero, and its sign alone flipped from
        // pixel to pixel (route-77: speckled sides of a pig at the edge of the picture).
        // The fragment in view space, from the clip position: x = (a,0,c,0), y = (0,b,e,0), w = -z.
        vec4 clip=pc.viewProjection*vec4(model*pc.anchor.w+pc.anchor.xyz,1);
        vec3 rx=vec3(pc.viewProjection[0][0],pc.viewProjection[1][0],pc.viewProjection[2][0]);
        vec3 ry=vec3(pc.viewProjection[0][1],pc.viewProjection[1][1],pc.viewProjection[2][1]);
        float z=-clip.w;
        vec3 at=vec3((clip.x-dot(rx,vz)*z)/dot(rx,vx),(clip.y-dot(ry,vz)*z)/dot(ry,vy),z);
        if(dot(view,at)>0.0)view=-view;
    }
    float packed=pc.viewY.w,alpha=mod(packed,256.0),skyFloor=floor(packed/256.0)/255.0;
    float cls=mod(pc.viewX.w,256.0),flags=floor(pc.viewX.w/256.0);
    // BOTW's sun and sky know nothing of Minecraft's roofs: its sky light darkens the surface.
    vec3 base=toLinear(tex.rgb*color.rgb)*pc.params.z;
    material=vec4(cls/255.0,flags/255.0,0,0);
    albedo=vec4(base*mix(skyFloor,1.0,curve(levels.y)),0);
    normal=vec4(view*0.5+0.5,alpha/255.0);
    emissive=vec4(base*curve(levels.x)*pc.params.w,0);
}
