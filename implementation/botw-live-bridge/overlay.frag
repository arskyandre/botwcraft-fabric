#version 450
layout(binding=1) uniform sampler2D overlay;
layout(location=0) in vec2 uv;
layout(location=0) out vec4 outputColor;
// Minecraft's pixels are encoded for the display (sRGB); the target they are drawn into holds
// linear light, which Cemu encodes when it presents (BOTW's TV buffer is an sRGB surface).
// Written as they came they were encoded twice: sent 19 was shown as 77, 93 as 163 (measured
// on the hearts and the hotbar, 2026-10-11). The colour is premultiplied by its alpha.
vec3 toLinear(vec3 c){return mix(c/12.92,pow((c+0.055)/1.055,vec3(2.4)),step(vec3(0.04045),c));}
void main(){
    vec4 t=texture(overlay,uv);
    vec3 colour=t.a>0.0?clamp(t.rgb/t.a,0.0,1.0):vec3(0);
    outputColor=vec4(toLinear(colour)*t.a,t.a);
}
