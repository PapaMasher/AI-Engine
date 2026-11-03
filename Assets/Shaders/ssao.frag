#version 450

// Screen-space ambient occlusion from the depth + normal prepass. Uses a
// hemisphere kernel rotated by a per-pixel hash.

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 256) uniform FrameConstants
{
    mat4 uViewProjection;
    mat4 uView;
    mat4 uProjection;
    mat4 uInvViewProjection;
    mat4 uInvProjection;
    mat4 uLightViewProjection;
    vec4 uCameraPosition;
    vec4 uLightDirection;
    vec4 uLightColor;
    vec4 uAmbientColor;
    vec4 uShadowParams;
    vec4 uScreenParams;
    vec4 uExposure;
    vec4 uIBLParams;
};

layout(set = 0, binding = 0) uniform texture2D uDepth;
layout(set = 0, binding = 1) uniform texture2D uNormal;
layout(set = 0, binding = 128) uniform sampler uPointClamp;

const int KERNEL_SIZE = 16;
const float RADIUS = 0.65;
const float BIAS = 0.025;

float Hash(vec2 p)
{
    return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453);
}

vec3 ViewPosition(vec2 uv, float depth)
{
    // NVRHI's Vulkan viewport uses D3D conventions: NDC +Y is up, so screen UV
    // (top-left origin) maps to ndc.y = 1 - 2 * uv.y.
    const vec4 clip = vec4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, depth, 1.0);
    const vec4 view = uInvProjection * clip;
    return view.xyz / view.w;
}

void main()
{
    const float depth = texture(sampler2D(uDepth, uPointClamp), vUV).r;
    if (depth >= 1.0)
    {
        outColor = vec4(1.0);
        return;
    }

    const vec3 viewPos = ViewPosition(vUV, depth);
    const vec3 normal = normalize(texture(sampler2D(uNormal, uPointClamp), vUV).xyz);

    const float randomAngle = Hash(vUV * uScreenParams.xy) * 6.28318530718;
    const vec3 randomVec = vec3(cos(randomAngle), sin(randomAngle), 0.0);
    const vec3 tangent = normalize(randomVec - normal * dot(randomVec, normal));
    const vec3 bitangent = cross(normal, tangent);
    const mat3 TBN = mat3(tangent, bitangent, normal);

    float occlusion = 0.0;

    for (int i = 0; i < KERNEL_SIZE; ++i)
    {
        const float fi = float(i);
        const float angle = fi * 2.39996322973;
        const float radius = sqrt((fi + 0.5) / float(KERNEL_SIZE));
        const vec3 hemisphere = vec3(cos(angle) * radius, sin(angle) * radius,
            sqrt(max(0.0, 1.0 - radius * radius)));

        const vec3 samplePos = viewPos + TBN * hemisphere * RADIUS;

        const vec4 offset = uProjection * vec4(samplePos, 1.0);
        const vec2 sampleUV = vec2((offset.x / offset.w) * 0.5 + 0.5,
            0.5 - (offset.y / offset.w) * 0.5);

        if (sampleUV.x < 0.0 || sampleUV.x > 1.0 || sampleUV.y < 0.0 || sampleUV.y > 1.0)
            continue;

        const float sampleDepth = texture(sampler2D(uDepth, uPointClamp), sampleUV).r;
        const float sceneZ = ViewPosition(sampleUV, sampleDepth).z;
        const float rangeCheck = smoothstep(0.0, 1.0, RADIUS / max(abs(viewPos.z - sceneZ), 1e-4));

        if (sceneZ >= samplePos.z + BIAS)
            occlusion += rangeCheck;
    }

    const float ao = 1.0 - occlusion / float(KERNEL_SIZE);
    outColor = vec4(clamp(ao, 0.0, 1.0));
}
