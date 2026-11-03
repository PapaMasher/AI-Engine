#version 450

// UI fragment shader: samples coverage from the font atlas and outputs a
// premultiplied-by-alpha colour. Drawn into the HDR target before tonemapping,
// so the RGB is boosted to stay near white after the ACES curve.

layout(location = 0) in vec2 vUV;
layout(location = 1) in vec4 vColor;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform texture2D uAtlas;
layout(set = 0, binding = 128) uniform sampler uLinearClamp;

void main()
{
    const float coverage = texture(sampler2D(uAtlas, uLinearClamp), vUV).a;
    outColor = vec4(vColor.rgb * 1.6, vColor.a * coverage);
}
