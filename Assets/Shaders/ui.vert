#version 450

// Screen-space UI vertex shader. Positions are pixels with the origin at the
// top-left; they are converted to NDC here.

layout(location = 0) in vec2 aPosition;
layout(location = 1) in vec2 aUV;
layout(location = 2) in vec4 aColor;

layout(location = 0) out vec2 vUV;
layout(location = 1) out vec4 vColor;

layout(push_constant) uniform UIConstants
{
    vec2 screenSize;
    vec2 padding;
} pc;

void main()
{
    vec2 ndc = vec2(
        (aPosition.x / pc.screenSize.x) * 2.0 - 1.0,
        1.0 - (aPosition.y / pc.screenSize.y) * 2.0);

    gl_Position = vec4(ndc, 0.0, 1.0);
    vUV = aUV;
    vColor = aColor;
}
