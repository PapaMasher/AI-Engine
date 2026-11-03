#version 450

// HDR -> display transform plus FXAA. The scene is rendered into a linear
// RGBA16F target; this pass applies an ACES filmic curve, manual sRGB
// encoding (the swap chain uses a UNORM format) and a 9-tap FXAA resolve.

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

vec3 TonemapSample(vec2 uv)
{
    const vec3 hdr = texture(uSource, uv).rgb;
    return pow(ACESFilm(hdr), vec3(1.0 / 2.2));
}

float Luma(vec3 color)
{
    return dot(color, vec3(0.299, 0.587, 0.114));
}

void main()
{
    const vec2 texel = 1.0 / vec2(textureSize(uSource, 0));

    const vec3 rgbM = TonemapSample(vUV);
    const vec3 rgbNW = TonemapSample(vUV + vec2(-texel.x, -texel.y));
    const vec3 rgbNE = TonemapSample(vUV + vec2(texel.x, -texel.y));
    const vec3 rgbSW = TonemapSample(vUV + vec2(-texel.x, texel.y));
    const vec3 rgbSE = TonemapSample(vUV + vec2(texel.x, texel.y));

    const float lumaM = Luma(rgbM);
    const float lumaNW = Luma(rgbNW);
    const float lumaNE = Luma(rgbNE);
    const float lumaSW = Luma(rgbSW);
    const float lumaSE = Luma(rgbSE);

    const float lumaMin = min(lumaM, min(min(lumaNW, lumaNE), min(lumaSW, lumaSE)));
    const float lumaMax = max(lumaM, max(max(lumaNW, lumaNE), max(lumaSW, lumaSE)));

    // Flat area: skip the extra taps.
    if ((lumaMax - lumaMin) < max(0.0312, lumaMax * 0.125))
    {
        outColor = vec4(rgbM, 1.0);
        return;
    }

    vec2 dir;
    dir.x = -((lumaNW + lumaNE) - (lumaSW + lumaSE));
    dir.y = ((lumaNW + lumaSW) - (lumaNE + lumaSE));

    const float dirReduce = max((lumaNW + lumaNE + lumaSW + lumaSE) * (0.25 / 8.0), 1.0 / 128.0);
    const float rcpDirMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + dirReduce);
    dir = clamp(dir * rcpDirMin, vec2(-8.0), vec2(8.0)) * texel;

    const vec3 rgbA = 0.5 * (
        TonemapSample(vUV + dir * (1.0 / 3.0 - 0.5)) +
        TonemapSample(vUV + dir * (2.0 / 3.0 - 0.5)));

    const vec3 rgbB = rgbA * 0.5 + 0.25 * (
        TonemapSample(vUV + dir * -0.5) +
        TonemapSample(vUV + dir * 0.5));

    const float lumaB = Luma(rgbB);
    outColor = vec4((lumaB < lumaMin || lumaB > lumaMax) ? rgbA : rgbB, 1.0);
}
