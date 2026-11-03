#version 450

// Cosine-convolved irradiance cubemap for diffuse image-based lighting.

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform textureCube uEnvironment;
layout(set = 0, binding = 128) uniform sampler uCubeSampler;

layout(push_constant) uniform PushConstants
{
    int uFace;
} pc;

const float PI = 3.14159265359;
const int SAMPLE_COUNT = 64;

vec3 FaceDirection(int face, vec2 st)
{
    if (face == 0) return normalize(vec3(1.0, -st.y, -st.x));
    if (face == 1) return normalize(vec3(-1.0, -st.y, st.x));
    if (face == 2) return normalize(vec3(st.x, 1.0, st.y));
    if (face == 3) return normalize(vec3(st.x, -1.0, -st.y));
    if (face == 4) return normalize(vec3(st.x, -st.y, 1.0));
    return normalize(vec3(-st.x, -st.y, -1.0));
}

vec2 Hammersley(uint i, uint n)
{
    uint bits = (i << 16u) | (i >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return vec2(float(i) / float(n), float(bits) * 2.3283064365386963e-10);
}

void main()
{
    const vec3 N = FaceDirection(pc.uFace, vUV * 2.0 - 1.0);

    const vec3 up = abs(N.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    const vec3 right = normalize(cross(up, N));
    const vec3 forward = cross(N, right);

    vec3 irradiance = vec3(0.0);

    for (uint i = 0u; i < SAMPLE_COUNT; ++i)
    {
        const vec2 xi = Hammersley(i, SAMPLE_COUNT);
        const float phi = 2.0 * PI * xi.x;
        const float cosTheta = sqrt(1.0 - xi.y);
        const float sinTheta = sqrt(xi.y);

        const vec3 local = vec3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
        const vec3 direction = normalize(right * local.x + forward * local.y + N * local.z);

        irradiance += texture(samplerCube(uEnvironment, uCubeSampler), direction).rgb;
    }

    irradiance = irradiance * (PI / float(SAMPLE_COUNT));
    outColor = vec4(irradiance, 1.0);
}
