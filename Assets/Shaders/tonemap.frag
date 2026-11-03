#version 450

// HDR -> display transform used by the final composite pass. The scene is
// rendered into a linear RGBA16F target; this pass applies exposure, an ACES
// filmic curve and manual sRGB encoding (the swap chain uses an UNORM format).

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D uSource;

vec3 ACESFilm(vec3 x)
{
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

void main()
{
    vec3 hdr = texture(uSource, vUV).rgb;
    vec3 mapped = ACESFilm(hdr);
    outColor = vec4(pow(mapped, vec3(1.0 / 2.2)), 1.0);
}
