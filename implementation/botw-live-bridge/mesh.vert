#version 450
layout(location=0) in vec3 position;
layout(location=1) in vec2 atlasUV;
layout(location=2) in vec4 tint;
layout(location=3) in uint light;
layout(location=4) in uint flags;
// params: target width and height in pixels, half an outline's width in pixels; the rest is the fragment shader's.
layout(push_constant,row_major) uniform Push { mat4 viewProjection; vec4 anchor; vec4 params; vec4 extra0; vec4 extra1; } pc;
layout(location=0) out vec2 uv;
layout(location=1) out vec4 color;
layout(location=2) out vec2 levels;
layout(location=3) flat out uint bits;
layout(location=4) out vec3 model;
vec4 project(vec3 p){return pc.viewProjection*vec4(p*pc.anchor.w+pc.anchor.xyz,1);}
void main(){
    vec4 clip=project(position);
    uv=atlasUV;color=tint;bits=flags;model=position;
    levels=vec2(light&255u,(light>>8u)&255u)/15.0;
    if((flags&256u)!=0u){
        // One end of an outline edge (live_scene.h build::line): u, v and the
        // light word hold the vector to the other end. The edge becomes a quad
        // of constant width on screen, as Minecraft's line shader makes it.
        vec4 other=project(position+vec3(atlasUV,uintBitsToFloat(light)));
        // Both ends in front of the eye before dividing (sead clip space: w is the view depth).
        const float nearW=0.01;
        if(clip.w<nearW&&other.w>nearW)clip=mix(clip,other,(nearW-clip.w)/(other.w-clip.w));
        else if(other.w<nearW&&clip.w>nearW)other=mix(other,clip,(nearW-other.w)/(clip.w-other.w));
        vec2 a=clip.xy/max(clip.w,nearW),b=other.xy/max(other.w,nearW);
        vec2 along=(b-a)*pc.params.xy;float len=length(along);along=len>1e-4?along/len:vec2(1,0);
        vec2 side=vec2(-along.y,along.x)*((flags&512u)!=0u?-1.0:1.0);
        clip.xy+=side*pc.params.z*2.0/pc.params.xy*clip.w;
        uv=vec2(0);levels=vec2(1);
    }
    clip.z=(clip.z+clip.w)*0.5; // Measured native sead device matrix is GL [-1,+1].
    gl_Position=clip;
}
