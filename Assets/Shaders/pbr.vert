#version 450

// Forward PBR vertex shader. Per-object data arrives through push constants;
// per-frame camera/light data comes from the frame constant buffer.

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoord;
layout(location = 3) in vec4 aTangent;

layout(location = 0) out vec3 vWorldPos;
layout(location = 1) out vec3 vNormal;
layout(location = 2) out vec2 vUV;
layout(location = 3) out vec3 vTangent;
layout(location = 4) out vec3 vBitangent;

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

layout(push_constant) uniform PushConstants
{
    mat4 uModel;
    vec4 uBaseColor;
    vec4 uParams;   // metallic, roughness, emissive strength, flags (1 = normal map)
    vec4 uEmissive; // rgb, unused
} pc;

void main()
{
    const vec4 world = pc.uModel * vec4(aPosition, 1.0);
    vWorldPos = world.xyz;

    const mat3 normalMatrix = transpose(inverse(mat3(pc.uModel)));
    vNormal = normalize(normalMatrix * aNormal);

    const vec3 tangent = normalize(mat3(pc.uModel) * aTangent.xyz);
    vTangent = tangent;
    vBitangent = normalize(cross(vNormal, tangent) * aTangent.w);

    vUV = aTexCoord;
    gl_Position = uViewProjection * world;
}
