#include "FullscreenTriangle.hlsli"

Texture2D<float4> g_reflectionRayHit : register(t0, space6);
Texture2D<float4> g_reflectionRayColor : register(t0, space7);
Texture2D<float4> g_reflectionRayMaterial : register(t0, space8);
SamplerState g_sampler : register(s0);

cbuffer LightingConstants : register(b2)
{
    float3 lightDirection;
    float iblIntensity;
    float3 lightColor;
    float diffuseIntensity;
    float4 backgroundColor;
    float skyboxEnabled;
    float skyboxPreview;
    float skyboxPreviewExposure;
    float lightPassDebugViewMode;
    float directLightEnabled;
    float diffuseIblEnabled;
    float specularIblEnabled;
    float emissiveEnabled;
    float iblDebugMip;
    float iblDebugExposure;
    float rayTracingSupported;
    float shadowMaskBlurEnabled;
    float reflectionHitOverlayEnabled;
    float reflectionHitOverlayIntensity;
    float reflectionHitOverlayMode;
    float reflectionContributionEnabled;
    float reflectionContributionIntensity;
    float reflectionContributionMaxDistance;
};

FullscreenVSOutput VSMain(uint vertexId : SV_VertexID)
{
    return FullscreenTriangleVS(vertexId);
}

float3 DecodeNormalOctahedron(float2 encodedNormal)
{
    float2 f = encodedNormal * 2.0 - 1.0;
    float3 normal = float3(f.x, f.y, 1.0 - abs(f.x) - abs(f.y));
    if (normal.z < 0.0)
    {
        float2 signNotZero = float2(normal.x >= 0.0 ? 1.0 : -1.0, normal.y >= 0.0 ? 1.0 : -1.0);
        normal.xy = (1.0 - abs(normal.yx)) * signNotZero;
    }

    return normalize(normal);
}

float4 PSMain(FullscreenVSOutput input) : SV_TARGET
{
    float4 reflectionHit = g_reflectionRayHit.Sample(g_sampler, input.uv);
    float3 hitColor = g_reflectionRayColor.Sample(g_sampler, input.uv).rgb;
    float4 hitMaterial = g_reflectionRayMaterial.Sample(g_sampler, input.uv);
    float hitRoughness = saturate(hitMaterial.y);
    float hitUnlit = saturate(hitMaterial.z);

    float3 hitNormal = DecodeNormalOctahedron(reflectionHit.zw);
    float ndotl = saturate(dot(hitNormal, normalize(lightDirection)));
    float3 directRadiance = hitColor * lightColor * diffuseIntensity * ndotl * directLightEnabled;
    float3 shadedHitColor = lerp(directRadiance, hitColor, hitUnlit);

    float distanceFade = saturate(1.0 - reflectionHit.x / max(reflectionContributionMaxDistance, 0.001));
    float strength = reflectionHit.y * distanceFade * (1.0 - hitRoughness) * reflectionContributionIntensity;
    return float4(shadedHitColor * strength, 1.0);
}
