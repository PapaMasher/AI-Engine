#version 450

layout(location = 0) in vec3 vViewNormal;
layout(location = 0) out vec4 outNormal;

void main()
{
    vec3 N = normalize(vViewNormal);

    // Match the two-sided forward pass so SSAO sees correct normals for both
    // sides of an imported mesh.
    if (!gl_FrontFacing)
        N = -N;

    outNormal = vec4(N, 1.0);
}
