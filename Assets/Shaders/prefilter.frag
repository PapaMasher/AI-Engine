#version 450

// GGX-prefiltered specular cubemap. Each mip level corresponds to a roughness
// value supplied through push constants.

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform textureCube uEnvironment;
layout(set = 0, binding = 128) uniform sampler uCubeSampler;

layout(push_constant) uniform PushConstants
{
    int uFace;
    float uRoughness;
    float uSourceMip;
} pc;

const float PI = 3.14159265359;
const int SAMPLE_COUNT = 128;

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

vec3 ImportanceSampleGGX(vec2 xi, vec3 N, float roughness)
{
    const float a = roughness * roughness;
    const float phi = 2.0 * PI * xi.x;
    const float cosTheta = sqrt((1.0 - xi.y) / (1.0 + (a * a - 1.0) * xi.y));
    const float sinTheta = sqrt(max(0.0, 1.0 - cosTheta * cosTheta));

    const vec3 H = vec3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);

    const vec3 up = abs(N.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    const vec3 tangent = normalize(cross(up, N));
    const vec3 bitangent = cross(N, tangent);

    return normalize(tangent * H.x + bitangent * H.y + N * H.z);
}

void main()
{
    const vec3 N = FaceDirection(pc.uFace, vUV * 2.0 - 1.0);
    const vec3 V = N;

    vec3 prefiltered = vec3(0.0);
    float totalWeight = 0.0;

    for (uint i = 0u; i < SAMPLE_COUNT; ++i)
    {
        const vec2 xi = Hammersley(i, SAMPLE_COUNT);
        const vec3 H = ImportanceSampleGGX(xi, N, pc.uRoughness);
        const vec3 L = normalize(2.0 * dot(V, H) * H - V);

        const float NdotL = max(dot(N, L), 0.0);
        if (NdotL > 0.0)
        {
            prefiltered += textureLod(samplerCube(uEnvironment, uCubeSampler), L, pc.uSourceMip).rgb * NdotL;
            totalWeight += NdotL;
        }
    }

    if (totalWeight > 0.0)
        prefiltered /= totalWeight;

    outColor = vec4(prefiltered, 1.0);
}
