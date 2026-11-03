#version 450

// Depth + view-space normal prepass used by SSAO and as the depth buffer for
// the forward shading pass.

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;

layout(location = 0) out vec3 vViewNormal;

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
    vec4 uParams;
    vec4 uEmissive;
} pc;

void main()
{
    vViewNormal = normalize(mat3(uView) * (transpose(inverse(mat3(pc.uModel))) * aNormal));
    gl_Position = uViewProjection * pc.uModel * vec4(aPosition, 1.0);
}
