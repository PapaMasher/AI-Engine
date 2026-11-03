#version 450

// Draws the environment cubemap as a fullscreen background.

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

layout(set = 0, binding = 0) uniform textureCube uEnvironment;
layout(set = 0, binding = 128) uniform sampler uCubeSampler;

void main()
{
    const vec4 clip = vec4(vUV.x * 2.0 - 1.0, 1.0 - vUV.y * 2.0, 1.0, 1.0);
    const vec4 world = uInvViewProjection * clip;
    const vec3 direction = normalize(world.xyz / world.w - uCameraPosition.xyz);

    const vec3 color = textureLod(samplerCube(uEnvironment, uCubeSampler), direction, uIBLParams.y).rgb;
    outColor = vec4(color * uExposure.x, 1.0);
}
