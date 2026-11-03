#version 450

// 4x4 box blur for the SSAO result.

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform texture2D uSource;
layout(set = 0, binding = 128) uniform sampler uPointClamp;

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

void main()
{
    const vec2 texel = uScreenParams.zw;
    float sum = 0.0;
    for (int x = -1; x <= 2; ++x)
        for (int y = -1; y <= 2; ++y)
            sum += texture(sampler2D(uSource, uPointClamp), vUV + vec2(x, y) * texel).r;

    outColor = vec4(sum / 16.0);
}
