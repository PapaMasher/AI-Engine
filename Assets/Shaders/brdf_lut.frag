#version 450

// Integrated BRDF lookup table: R = scale, G = bias for the split-sum
// approximation of the specular IBL term.

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

const float PI = 3.14159265359;
const int SAMPLE_COUNT = 128;

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

float GeometrySchlickGGX(float NdotV, float roughness)
{
    const float a = roughness;
    const float k = (a * a) / 2.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness)
{
    return GeometrySchlickGGX(max(dot(N, V), 0.0), roughness)
        * GeometrySchlickGGX(max(dot(N, L), 0.0), roughness);
}

void main()
{
    const float NdotV = max(vUV.x, 1e-4);
    const float roughness = vUV.y;

    const vec3 V = vec3(sqrt(1.0 - NdotV * NdotV), 0.0, NdotV);
    const vec3 N = vec3(0.0, 0.0, 1.0);

    float scale = 0.0;
    float bias = 0.0;

    for (uint i = 0u; i < SAMPLE_COUNT; ++i)
    {
        const vec2 xi = Hammersley(i, SAMPLE_COUNT);
        const vec3 H = ImportanceSampleGGX(xi, N, roughness);
        const vec3 L = normalize(2.0 * dot(V, H) * H - V);

        const float NdotL = max(L.z, 0.0);
        const float NdotH = max(H.z, 0.0);
        const float VdotH = max(dot(V, H), 0.0);

        if (NdotL > 0.0)
        {
            const float G = GeometrySmith(N, V, L, roughness);
            const float GVis = (G * VdotH) / max(NdotH * NdotV, 1e-4);
            const float Fc = pow(1.0 - VdotH, 5.0);

            scale += (1.0 - Fc) * GVis;
            bias += Fc * GVis;
        }
    }

    outColor = vec4(scale / float(SAMPLE_COUNT), bias / float(SAMPLE_COUNT), 0.0, 1.0);
}
