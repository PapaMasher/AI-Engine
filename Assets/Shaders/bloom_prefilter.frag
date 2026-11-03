#version 450

// Bloom prefilter: extracts bright regions with a soft knee so highlights
// bloom without clipping hard edges.

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
    vec3 color = texture(sampler2D(uSource, uLinearClamp), vUV).rgb;

    const float brightness = max(color.r, max(color.g, color.b));
    const float soft = clamp(brightness - pc.threshold + pc.knee, 0.0, 2.0 * pc.knee);
    const float kneeWeight = soft * soft / (4.0 * pc.knee + 1e-4);
    const float contribution = max(kneeWeight, brightness - pc.threshold) / max(brightness, 1e-4);

    outColor = vec4(color * contribution, 1.0);
}
