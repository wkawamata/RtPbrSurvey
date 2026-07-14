#include "FullscreenTriangle.hlsli"
#include "PbrLighting.hlsli"

Texture2D<float4> g_normal : register(t1, space3);
Texture2D<float4> g_pbrParams : register(t4, space3);
Texture2D<float> g_depth : register(t6, space3);
TextureCube<float4> g_environmentMap : register(t0, space5);
TextureCube<float4> g_diffuseIrradianceMap : register(t1, space5);
TextureCube<float4> g_specularPrefilterMap : register(t2, space5);
Texture2D<float2> g_brdfLut : register(t3, space5);
Texture2D<float4> g_reflectionRayHit : register(t0, space6);
Texture2D<float4> g_reflectionRayColor : register(t0, space7);
Texture2D<float4> g_reflectionRayMaterial : register(t0, space8);
Texture2D<float4> g_reflectionRayEmission : register(t0, space10);
SamplerState g_sampler : register(s0);

static const float PI = 3.14159265;
static const float SPECULAR_PREFILTER_MAX_MIP = 5.0;

cbuffer ConstantBuffer : register(b0)
{
    float4x4 viewProj;
    float4x4 prevViewProj;
    float4x4 invViewProj;
    float3 cameraPosition;
    float constantBufferPadding;
};

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

float3 ReconstructWorldPosition(float2 uv, float depth)
{
    float2 ndc = float2(uv.x * 2.0 - 1.0, (1.0 - uv.y) * 2.0 - 1.0);
    float4 clipPos = float4(ndc, depth, 1.0);
    float4 worldPos = mul(clipPos, invViewProj);
    return worldPos.xyz / worldPos.w;
}

PbrSurface MakeReflectionHitSurface(float3 albedo, float4 material, float3 normal, float3 emissive)
{
    return MakePbrSurface(albedo, normal, emissive, material.x, material.y, 1.0, material.z);
}

float4 PSMain(FullscreenVSOutput input) : SV_TARGET
{
    float depth = g_depth.Load(int3(input.position.xy, 0));
    if (depth >= 1.0)
    {
        return float4(0.0, 0.0, 0.0, 1.0);
    }

    float4 reflectionHit = g_reflectionRayHit.Sample(g_sampler, input.uv);
    float3 hitColor = g_reflectionRayColor.Sample(g_sampler, input.uv).rgb;
    float4 hitMaterial = g_reflectionRayMaterial.Sample(g_sampler, input.uv);
    float3 hitEmission = g_reflectionRayEmission.Sample(g_sampler, input.uv).rgb;

    float3 normal = normalize(g_normal.Sample(g_sampler, input.uv).rgb);
    float visibleRoughness = saturate(g_pbrParams.Sample(g_sampler, input.uv).g);
    float3 worldPos = ReconstructWorldPosition(input.uv, depth);
    float3 viewDir = normalize(cameraPosition - worldPos);
    float3 reflectionDir = reflect(-viewDir, normal);

    if (reflectionHit.y <= 0.0)
    {
        float missSpecularMip = visibleRoughness * SPECULAR_PREFILTER_MAX_MIP;
        float3 environmentRadiance =
            g_specularPrefilterMap.SampleLevel(g_sampler, reflectionDir, missSpecularMip).rgb * iblIntensity *
            specularIblEnabled;
        float missStrength = (1.0 - visibleRoughness) * reflectionContributionIntensity;
        return float4(environmentRadiance * missStrength, 1.0);
    }

    float3 hitNormal = DecodeNormalOctahedron(reflectionHit.zw);
    PbrSurface hitSurface = MakeReflectionHitSurface(hitColor, hitMaterial, hitNormal, hitEmission);
    float3 hitViewDir = -reflectionDir;
    float3 diffuseIrradiance = g_diffuseIrradianceMap.Sample(g_sampler, hitSurface.normal).rgb;
    float specularMip = hitSurface.roughness * SPECULAR_PREFILTER_MAX_MIP;
    float3 hitSpecularDirection = reflect(reflectionDir, hitSurface.normal);
    float hitNdotV = saturate(dot(hitSurface.normal, -reflectionDir));
    float2 hitBrdf = g_brdfLut.Sample(g_sampler, float2(hitNdotV, hitSurface.roughness)).rg;
    float3 hitEnvironmentSpecular = g_specularPrefilterMap.SampleLevel(g_sampler, hitSpecularDirection, specularMip).rgb;
    float3 lightDir = normalize(lightDirection);
    float3 lightRadiance = lightColor * diffuseIntensity;
    PbrRadianceComponents hitRadiance = EvaluatePbrRadianceComponents(hitSurface,
                                                                      hitViewDir,
                                                                      lightDir,
                                                                      lightRadiance,
                                                                      diffuseIrradiance,
                                                                      hitEnvironmentSpecular,
                                                                      hitBrdf,
                                                                      hitNdotV,
                                                                      directLightEnabled,
                                                                      iblIntensity * diffuseIblEnabled,
                                                                      iblIntensity * specularIblEnabled);
    float3 shadedHitColor = EvaluatePbrSurfaceRadiance(hitSurface, hitRadiance, emissiveEnabled);

    float distanceFade = saturate(1.0 - reflectionHit.x / max(reflectionContributionMaxDistance, 0.001));
    float strength = reflectionHit.y * distanceFade * (1.0 - visibleRoughness) * reflectionContributionIntensity;
    return float4(shadedHitColor * strength, 1.0);
}
