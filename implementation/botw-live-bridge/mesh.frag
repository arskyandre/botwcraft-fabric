#version 450
layout(binding=0) uniform sampler2D atlas;
layout(location=0) in vec2 uv;
layout(location=1) in vec4 color;
layout(location=2) in vec2 levels;
layout(location=3) flat in uint bits;
layout(location=4) in vec3 model;
layout(location=0) out vec4 outputColor;
float curve(float l){return l/(4.0-3.0*l);}
// The result is a display colour (Minecraft's arithmetic is done on encoded values); the target
// holds linear light, encoded when Cemu presents (overlay.frag).
vec3 toLinear(vec3 c){return mix(c/12.92,pow((c+0.055)/1.055,vec3(2.4)),step(vec3(0.04045),c));}
// Minecraft's face shading in the overworld: top 1, bottom 0.5, north and
// south 0.8, west and east 0.6. The mod takes it out of the block colours and
// names the face instead (Direction ordinal + 1; 0 for plants, which are not
// shaded); entity models ask for the triangle's own normal (7), which is
// taken towards the viewer as WorldRender.cpp does.
float faceShade(){
    uint n=(bits>>4u)&7u;
    if(n==0u)return 1.0;
    if(n==7u){
        vec3 f=cross(dFdy(model),dFdx(model));float l=dot(f,f);if(l<1e-20)return 1.0;f*=inversesqrt(l);
        return 0.6*f.x*f.x+0.8*f.z*f.z+f.y*f.y*(f.y>0.0?1.0:0.5);
    }
    return n==1u?0.5:n==2u?1.0:n<=4u?0.8:0.6;
}
void main(){
    bool translucent=(bits&2u)!=0u;
    vec4 tex=(bits&4u)!=0u?vec4(1):texture(atlas,uv);
    // WorldRender.cpp tests every texel that is not blended.
    if(!translucent&&tex.a<0.5)discard;
    if((bits&256u)!=0u){outputColor=vec4(toLinear(color.rgb),color.a);return;} // the block outline: its colour as it is
    float lighting=max(curve(levels.x),mix(0.2,1.0,curve(levels.y)));
    float alpha=translucent?tex.a*color.a:1.0;
    if(translucent&&alpha<0.004)discard;
    outputColor=vec4(toLinear(clamp(tex.rgb*color.rgb*lighting*faceShade(),0.0,1.0)),alpha);
}
