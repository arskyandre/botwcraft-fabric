#version 450
// Minecraft's blended geometry (water, glass, ice, particles, cracks, the outline) into BOTW's
// HDR scene colour, after BOTW's own translucent draws and before its bloom (report 44).
// Lit here with the sun BOTW's lighting pass used in the same frame.
layout(binding=0) uniform sampler2D atlas;
// BOTW's lighting pass output at half size: red is its sun shadow at the opaque surface behind this pixel.
layout(set=1,binding=0) uniform sampler2D shade;
// sun.xyz: direction to the sun in world space; sun.w: ambient light as a part of the sun's.
// sunColour.rgb: in BOTW's HDR units; sunColour.w: brightness of full block light.
layout(push_constant,row_major) uniform Push { mat4 viewProjection; vec4 anchor; vec4 params; vec4 sun; vec4 sunColour; } pc;
layout(location=0) in vec2 uv;
layout(location=1) in vec4 color;
layout(location=2) in vec2 levels;
layout(location=3) flat in uint bits;
layout(location=4) in vec3 model;
layout(location=0) out vec4 outputColor;
float curve(float l){return l/(4.0-3.0*l);}
vec3 toLinear(vec3 c){return mix(c/12.92,pow((c+0.055)/1.055,vec3(2.4)),step(vec3(0.04045),c));}
void main(){
    vec4 tex=(bits&4u)!=0u?vec4(1):texture(atlas,uv);
    bool translucent=(bits&2u)!=0u;
    if(!translucent&&tex.a<0.5)discard;
    float alpha=translucent?tex.a*color.a:1.0;
    if((bits&256u)!=0u){outputColor=vec4(toLinear(color.rgb)*pc.sunColour.rgb*pc.sun.w,color.a);return;} // the block outline
    if(translucent&&alpha<0.004)discard;
    // Faces the mod names are lit by their own normal; plants, particles and entity quads
    // (which face the camera or have no single side) take a fixed part of the sun.
    uint n=(bits>>4u)&7u;float facing=0.6;
    if(n>=1u&&n<=6u){
        vec3 world=n==1u?vec3(0,-1,0):n==2u?vec3(0,1,0):n==3u?vec3(0,0,-1):n==4u?vec3(0,0,1):n==5u?vec3(-1,0,0):vec3(1,0,0);
        facing=max(dot(world,pc.sun.xyz),0.0);
    }
    float sky=curve(levels.y),lit=texture(shade,gl_FragCoord.xy*0.5/vec2(textureSize(shade,0))).r;
    vec3 light=pc.sunColour.rgb*(facing*sky*lit+pc.sun.w*mix(0.2,1.0,sky))+vec3(curve(levels.x)*pc.sunColour.w);
    outputColor=vec4(toLinear(tex.rgb*color.rgb)*light,alpha);
}
