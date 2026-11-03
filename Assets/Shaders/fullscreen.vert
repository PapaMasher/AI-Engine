#version 450

// Fullscreen triangle generated from the vertex index: no vertex buffer is
// needed for post-processing passes.
layout(location = 0) out vec2 vUV;

void main()
{
    vec2 position = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    vUV = position;
    gl_Position = vec4(position * 2.0 - 1.0, 0.0, 1.0);
}
