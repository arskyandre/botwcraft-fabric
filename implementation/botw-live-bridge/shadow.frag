#version 450
// Minecraft's opaque and cutout geometry as casters in BOTW's shadow cascades: depth only.
layout(binding=0) uniform sampler2D atlas;
layout(location=0) in vec2 uv;
layout(location=3) flat in uint bits;
void main(){
    if((bits&4u)==0u&&texture(atlas,uv).a<0.5)discard;
}
