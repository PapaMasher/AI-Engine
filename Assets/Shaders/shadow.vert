#version 450

// Depth-only shadow map pass for the directional light.

layout(location = 0) in vec3 aPosition;

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
    gl_Position = uLightViewProjection * pc.uModel * vec4(aPosition, 1.0);
}
