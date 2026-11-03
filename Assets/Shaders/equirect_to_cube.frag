#version 450

// Converts an equirectangular HDR panorama into one cubemap face.

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform texture2D uEquirect;
layout(set = 0, binding = 128) uniform sampler uLinearClamp;

layout(push_constant) uniform PushConstants
{
    int uFace;
} pc;

const float PI = 3.14159265359;

void main()
{
    const vec2 st = vUV * 2.0 - 1.0;
    vec3 direction;

    if (pc.uFace == 0)       direction = vec3(1.0, -st.y, -st.x);
    else if (pc.uFace == 1)  direction = vec3(-1.0, -st.y, st.x);
    else if (pc.uFace == 2)  direction = vec3(st.x, 1.0, st.y);
    else if (pc.uFace == 3)  direction = vec3(st.x, -1.0, -st.y);
    else if (pc.uFace == 4)  direction = vec3(st.x, -st.y, 1.0);
    else                     direction = vec3(-st.x, -st.y, -1.0);

    direction = normalize(direction);

    const vec2 equirectUV = vec2(
        atan(direction.z, direction.x) / (2.0 * PI) + 0.5,
        0.5 - asin(clamp(direction.y, -1.0, 1.0)) / PI);

    outColor = vec4(texture(sampler2D(uEquirect, uLinearClamp), equirectUV).rgb, 1.0);
}
