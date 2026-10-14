#version 450
layout(location=0) out vec2 uv;
void main(){
    uv=vec2((gl_VertexIndex<<1)&2,gl_VertexIndex&2);
    // Negative native viewport: +Y clip is screen top. PNG rows are top-down.
    gl_Position=vec4(uv.x*2-1,1-uv.y*2,0,1);
}
