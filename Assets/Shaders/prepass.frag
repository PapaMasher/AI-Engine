#version 450

layout(location = 0) in vec3 vViewNormal;
layout(location = 0) out vec4 outNormal;

void main()
{
    outNormal = vec4(normalize(vViewNormal), 1.0);
}
