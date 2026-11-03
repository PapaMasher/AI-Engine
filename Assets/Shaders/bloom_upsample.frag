#version 450

// 9-tap tent upsample, blended additively into the next larger bloom level so
// wide highlights accumulate smoothly.

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform texture2D uSource;
layout(set = 0, binding = 128) uniform sampler uLinearClamp;

layout(push_constant) uniform BloomConstants
{
    float threshold;
    float knee;
    float intensity;
    float padding;
} pc;

vec3 sampleSource(vec2 uv)
{
    return texture(sampler2D(uSource, uLinearClamp), uv).rgb;
}

void main()
{
    const vec2 texel = 1.0 / vec2(textureSize(sampler2D(uSource, uLinearClamp), 0));

    vec3 result = sampleSource(vUV + texel * vec2(-1.0, -1.0)) * 1.0;
    result += sampleSource(vUV + texel * vec2(0.0, -1.0)) * 2.0;
    result += sampleSource(vUV + texel * vec2(1.0, -1.0)) * 1.0;
    result += sampleSource(vUV + texel * vec2(-1.0, 0.0)) * 2.0;
    result += sampleSource(vUV) * 4.0;
    result += sampleSource(vUV + texel * vec2(1.0, 0.0)) * 2.0;
    result += sampleSource(vUV + texel * vec2(-1.0, 1.0)) * 1.0;
    result += sampleSource(vUV + texel * vec2(0.0, 1.0)) * 2.0;
    result += sampleSource(vUV + texel * vec2(1.0, 1.0)) * 1.0;

    outColor = vec4(result / 16.0, 1.0);
}
