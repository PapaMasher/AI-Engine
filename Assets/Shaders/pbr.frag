#version 450

// Metallic/roughness PBR shading with a directional light, PCF shadows,
// image-based lighting (irradiance + prefiltered specular + BRDF LUT) and
// screen-space ambient occlusion.

layout(location = 0) in vec3 vWorldPos;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec2 vUV;
layout(location = 3) in vec3 vTangent;
layout(location = 4) in vec3 vBitangent;

layout(location = 0) out vec4 outColor;

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

    vec4 uPointLightPosition[8];
    vec4 uPointLightColor[8];
};

layout(set = 0, binding = 0) uniform texture2D uBaseColorTex;
layout(set = 0, binding = 1) uniform texture2D uNormalTex;
layout(set = 0, binding = 2) uniform texture2D uMetallicRoughnessTex;
layout(set = 0, binding = 3) uniform texture2D uOcclusionTex;
layout(set = 0, binding = 4) uniform texture2D uEmissiveTex;
layout(set = 0, binding = 5) uniform texture2D uShadowMap;
layout(set = 0, binding = 6) uniform textureCube uIrradiance;
layout(set = 0, binding = 7) uniform textureCube uPrefilter;
layout(set = 0, binding = 8) uniform texture2D uBrdfLUT;
layout(set = 0, binding = 9) uniform texture2D uSSAO;

layout(set = 0, binding = 128) uniform sampler uLinearRepeat;
layout(set = 0, binding = 129) uniform sampler uShadowSampler;
layout(set = 0, binding = 130) uniform sampler uCubeSampler;

layout(push_constant) uniform PushConstants
{
    mat4 uModel;
    vec4 uBaseColor;
    vec4 uParams;   // metallic, roughness, emissive strength, flags (1 = normal map)
    vec4 uEmissive; // rgb, unused
} pc;

const float PI = 3.14159265359;

float DistributionGGX(vec3 N, vec3 H, float roughness)
{
    const float a = roughness * roughness;
    const float a2 = a * a;
    const float NdotH = max(dot(N, H), 0.0);
    const float NdotH2 = NdotH * NdotH;

    const float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    return a2 / max(PI * denom * denom, 1e-6);
}

float GeometrySchlickGGX(float NdotV, float roughness)
{
    const float r = roughness + 1.0;
    const float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness)
{
    const float NdotV = max(dot(N, V), 0.0);
    const float NdotL = max(dot(N, L), 0.0);
    return GeometrySchlickGGX(NdotV, roughness) * GeometrySchlickGGX(NdotL, roughness);
}

vec3 FresnelSchlick(float cosTheta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

vec3 FresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness)
{
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

float ShadowFactor(vec3 worldPos, vec3 N, vec3 L)
{
    const vec4 lightSpace = uLightViewProjection * vec4(worldPos + N * 0.02, 1.0);
    const vec3 proj = lightSpace.xyz / lightSpace.w;

    // NDC +Y is up while the shadow map's V axis grows downwards.
    const vec2 shadowUV = vec2(proj.x * 0.5 + 0.5, 0.5 - proj.y * 0.5);

    if (shadowUV.x < 0.0 || shadowUV.x > 1.0 || shadowUV.y < 0.0 || shadowUV.y > 1.0 || proj.z > 1.0)
        return 1.0;

    const float bias = max(0.0015 * (1.0 - dot(N, L)), 0.0002);
    const vec2 texelSize = vec2(uShadowParams.x);

    float shadow = 0.0;
    for (int x = -1; x <= 1; ++x)
    {
        for (int y = -1; y <= 1; ++y)
        {
            const float depth = texture(sampler2D(uShadowMap, uShadowSampler), shadowUV + vec2(x, y) * texelSize).r;
            shadow += (proj.z - bias) > depth ? 0.0 : 1.0;
        }
    }
    return shadow / 9.0;
}

void main()
{
    const vec2 screenUV = gl_FragCoord.xy * uScreenParams.zw;

    vec4 baseColor = pc.uBaseColor * texture(sampler2D(uBaseColorTex, uLinearRepeat), vUV);
    if (baseColor.a < 0.01)
        discard;

    vec3 N = normalize(vNormal);
    if (pc.uParams.w > 0.5)
    {
        const vec3 tangentNormal = texture(sampler2D(uNormalTex, uLinearRepeat), vUV).xyz * 2.0 - 1.0;
        const mat3 TBN = mat3(normalize(vTangent), normalize(vBitangent), N);
        N = normalize(TBN * tangentNormal);
    }

    const vec4 metallicRoughness = texture(sampler2D(uMetallicRoughnessTex, uLinearRepeat), vUV);
    const float metallic = clamp(pc.uParams.x * metallicRoughness.b, 0.0, 1.0);
    const float roughness = clamp(pc.uParams.y * metallicRoughness.g, 0.04, 1.0);

    const float materialAO = texture(sampler2D(uOcclusionTex, uLinearRepeat), vUV).r;
    const float ssao = texture(sampler2D(uSSAO, uLinearRepeat), screenUV).r;
    const float ao = materialAO * ssao;

    const vec3 emissive = pc.uEmissive.rgb * pc.uParams.z * texture(sampler2D(uEmissiveTex, uLinearRepeat), vUV).rgb;

    const vec3 V = normalize(uCameraPosition.xyz - vWorldPos);
    const vec3 L = normalize(-uLightDirection.xyz);
    const vec3 H = normalize(V + L);

    const float NdotL = max(dot(N, L), 0.0);
    const float NdotV = max(dot(N, V), 1e-4);
    const float NdotH = max(dot(N, H), 0.0);
    const float VdotH = max(dot(V, H), 0.0);

    const vec3 F0 = mix(vec3(0.04), baseColor.rgb, metallic);

    const float D = DistributionGGX(N, H, roughness);
    const float G = GeometrySmith(N, V, L, roughness);
    const vec3 F = FresnelSchlick(VdotH, F0);

    const vec3 specular = (D * G * F) / max(4.0 * NdotV * NdotL, 1e-4);
    const vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);

    const float shadow = uLightDirection.w > 0.0 ? ShadowFactor(vWorldPos, N, L) : 1.0;
    vec3 direct = (kD * baseColor.rgb / PI + specular) * uLightColor.rgb * uLightDirection.w * NdotL * shadow;

    // Point lights.
    const int pointLightCount = int(uIBLParams.z);
    for (int i = 0; i < pointLightCount; ++i)
    {
        const vec3 lightPosition = uPointLightPosition[i].xyz;
        const float range = max(uPointLightPosition[i].w, 1e-3);
        const vec3 lightColor = uPointLightColor[i].rgb * uPointLightColor[i].w;

        const vec3 toLight = lightPosition - vWorldPos;
        const float distanceSquared = dot(toLight, toLight);
        const float distance = sqrt(distanceSquared);
        const vec3 Lp = toLight / max(distance, 1e-4);

        const float attenuation = clamp(1.0 - distanceSquared / (range * range), 0.0, 1.0);
        const float falloff = attenuation * attenuation / (distanceSquared + 1.0);

        const float NdotLp = max(dot(N, Lp), 0.0);
        if (NdotLp <= 0.0)
            continue;

        const vec3 Hp = normalize(V + Lp);
        const float Dp = DistributionGGX(N, Hp, roughness);
        const float Gp = GeometrySmith(N, V, Lp, roughness);
        const vec3 Fp = FresnelSchlick(max(dot(V, Hp), 0.0), F0);
        const vec3 specularP = (Dp * Gp * Fp) / max(4.0 * NdotV * NdotLp, 1e-4);
        const vec3 kDp = (vec3(1.0) - Fp) * (1.0 - metallic);

        direct += (kDp * baseColor.rgb / PI + specularP) * lightColor * NdotLp * falloff;
    }

    // Image-based lighting.
    const vec3 irradiance = texture(samplerCube(uIrradiance, uCubeSampler), N).rgb;
    const vec3 diffuseIBL = irradiance * baseColor.rgb;

    const vec3 R = reflect(-V, N);
    const float maxMip = uIBLParams.x;
    const vec3 prefiltered = textureLod(samplerCube(uPrefilter, uCubeSampler), R, roughness * maxMip).rgb;
    const vec2 brdf = texture(sampler2D(uBrdfLUT, uLinearRepeat), vec2(NdotV, roughness)).rg;
    const vec3 F_ibl = FresnelSchlickRoughness(NdotV, F0, roughness);
    const vec3 specularIBL = prefiltered * (F_ibl * brdf.x + brdf.y);

    const vec3 ambient = (kD * diffuseIBL + specularIBL) * uAmbientColor.w * ao;

    outColor = vec4((direct + ambient + emissive) * uExposure.x, baseColor.a);
}
