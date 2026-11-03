#version 450

// 13-tap downsample (Jimenez / Call of Duty style) used to build the bloom
// mip chain without fireflies.

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

    vec3 a = sampleSource(vUV + texel * vec2(-2.0, 2.0));
    vec3 b = sampleSource(vUV + texel * vec2(0.0, 2.0));
    vec3 c = sampleSource(vUV + texel * vec2(2.0, 2.0));
    vec3 d = sampleSource(vUV + texel * vec2(-2.0, 0.0));
    vec3 e = sampleSource(vUV);
    vec3 f = sampleSource(vUV + texel * vec2(2.0, 0.0));
    vec3 g = sampleSource(vUV + texel * vec2(-2.0, -2.0));
    vec3 h = sampleSource(vUV + texel * vec2(0.0, -2.0));
    vec3 i = sampleSource(vUV + texel * vec2(2.0, -2.0));
    vec3 j = sampleSource(vUV + texel * vec2(-1.0, 1.0));
    vec3 k = sampleSource(vUV + texel * vec2(1.0, 1.0));
    vec3 l = sampleSource(vUV + texel * vec2(-1.0, -1.0));
    vec3 m = sampleSource(vUV + texel * vec2(1.0, -1.0));

    vec3 result = e * 0.125;
    result += (a + c + g + i) * 0.03125;
    result += (b + d + f + h) * 0.0625;
    result += (j + k + l + m) * 0.125;

    outColor = vec4(result, 1.0);
}
