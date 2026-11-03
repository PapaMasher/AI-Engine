#version 450

// Adds the bloom mip chain back into the HDR scene target. Blending is
// additive (One / One), so this pass only outputs the scaled bloom colour.

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

void main()
{
    outColor = vec4(texture(sampler2D(uSource, uLinearClamp), vUV).rgb * pc.intensity, 1.0);
}
