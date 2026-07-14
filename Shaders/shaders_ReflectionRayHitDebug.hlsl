#include "FullscreenTriangle.hlsli"
#include "PbrLighting.hlsli"

Texture2D<float4> g_normal : register(t1, space3);
Texture2D<float4> g_pbrParams : register(t4, space3);
Texture2D<float> g_depth : register(t6, space3);
TextureCube<float4> g_environmentMap : register(t0, space5);
TextureCube<float4> g_diffuseIrradianceMap : register(t1, space5);
TextureCube<float4> g_specularPrefilterMap : register(t2, space5);
Texture2D<float2> g_brdfLut : register(t3, space5);
Texture2D<float4> g_reflectionRayHit : register(t0, space4);
Texture2D<float4> g_reflectionRayColor : register(t0, space7);
Texture2D<float4> g_reflectionRayMaterial : register(t0, space8);
Texture2D<float4> g_reflectionRadiance : register(t0, space9);
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

cbuffer ReflectionRayHitDebugConstants : register(b1)
{
    uint debugTarget;
    float contributionMaxDistance;
    float contributionIntensity;
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

float3 ComputeDirectRadiance(float3 albedo, float metallic, float roughness, float3 normal, float3 viewDir)
{
    float3 lightDir = normalize(lightDirection);
    float3 radiance = lightColor * diffuseIntensity;
    return EvaluatePbrDirectLighting(albedo, metallic, roughness, normal, viewDir, lightDir, radiance) *
           directLightEnabled;
}

PbrSurface MakeReflectionHitSurface(float3 albedo, float4 material, float3 normal, float3 emissive)
{
    PbrSurface surface;
    surface.albedo = albedo;
    surface.normal = normal;
    surface.emissive = emissive;
    surface.metallic = saturate(material.x);
    surface.roughness = saturate(material.y);
    surface.ambientOcclusion = 1.0;
    surface.unlit = saturate(material.z);
    return surface;
}

FullscreenVSOutput VSMain(uint vertexId : SV_VertexID)
{
    return FullscreenTriangleVS(vertexId);
}

float4 PSMain(FullscreenVSOutput input) : SV_TARGET
{
    float4 rayHit = g_reflectionRayHit.Sample(g_sampler, input.uv);
    float3 rayColor = g_reflectionRayColor.Sample(g_sampler, input.uv).rgb;
    float4 rayMaterial = g_reflectionRayMaterial.Sample(g_sampler, input.uv);
    float3 rayEmission = g_reflectionRayEmission.Sample(g_sampler, input.uv).rgb;
    float visibleRoughness = saturate(g_pbrParams.Sample(g_sampler, input.uv).g);
    float hitDistance = rayHit.x;
    float hitFlag = rayHit.y;

    if (debugTarget == 0)
    {
        return float4(hitFlag, hitFlag, hitFlag, 1.0);
    }

    if (debugTarget == 2)
    {
        if (hitFlag <= 0.0)
        {
            return float4(0.0, 0.0, 0.0, 1.0);
        }

        float3 normal = DecodeNormalOctahedron(rayHit.zw);
        return float4(normal * 0.5 + 0.5, 1.0);
    }

    if (debugTarget == 3)
    {
        // Debug target 3 visualizes the current ReflectionRayColor payload: hit albedo.
        return float4(rayColor / (1.0 + rayColor), 1.0);
    }

    if (debugTarget == 4)
    {
        float distanceFade = saturate(1.0 - hitDistance / max(contributionMaxDistance, 0.001));
        return float4(distanceFade.xxx * hitFlag, 1.0);
    }

    if (debugTarget == 5)
    {
        float distanceFade = saturate(1.0 - hitDistance / max(contributionMaxDistance, 0.001));
        float contributionStrength = hitFlag * distanceFade * (1.0 - visibleRoughness) * contributionIntensity;
        return float4(contributionStrength.xxx, 1.0);
    }

    if (debugTarget == 6)
    {
        return float4(rayMaterial.x, rayMaterial.y, rayMaterial.z, 1.0);
    }

    if (debugTarget == 7)
    {
        float3 radiance = g_reflectionRadiance.Sample(g_sampler, input.uv).rgb;
        return float4(radiance / (1.0 + radiance), 1.0);
    }

    if (debugTarget == 8)
    {
        return float4(rayEmission / (1.0 + rayEmission), 1.0);
    }

    if (debugTarget >= 9 && debugTarget <= 12)
    {
        if (hitFlag <= 0.0)
        {
            return float4(0.0, 0.0, 0.0, 1.0);
        }

        float depth = g_depth.Sample(g_sampler, input.uv);
        if (depth >= 1.0)
        {
            return float4(0.0, 0.0, 0.0, 1.0);
        }

        float3 visibleNormal = normalize(g_normal.Sample(g_sampler, input.uv).rgb);
        float3 worldPos = ReconstructWorldPosition(input.uv, depth);
        float3 viewDir = normalize(cameraPosition - worldPos);
        float3 reflectionDir = reflect(-viewDir, visibleNormal);
        float3 hitNormal = DecodeNormalOctahedron(rayHit.zw);
        PbrSurface hitSurface = MakeReflectionHitSurface(rayColor, rayMaterial, hitNormal, rayEmission);
        float distanceFade = saturate(1.0 - hitDistance / max(contributionMaxDistance, 0.001));
        float strength = hitFlag * distanceFade * (1.0 - visibleRoughness) * contributionIntensity;

        float3 hitViewDir = -reflectionDir;
        float directRoughness = max(hitSurface.roughness, 0.04);
        float3 directRadiance =
            ComputeDirectRadiance(
                hitSurface.albedo, hitSurface.metallic, directRoughness, hitSurface.normal, hitViewDir);
        float3 diffuseIrradiance = g_diffuseIrradianceMap.Sample(g_sampler, hitSurface.normal).rgb;
        float3 diffuseIbl = EvaluatePbrDiffuseIbl(
                                diffuseIrradiance, hitSurface.albedo, hitSurface.metallic,
                                hitSurface.ambientOcclusion) *
                            iblIntensity *
                            diffuseIblEnabled;
        float3 hitF0 = PbrF0(hitSurface.albedo, hitSurface.metallic);
        float specularMip = hitSurface.roughness * SPECULAR_PREFILTER_MAX_MIP;
        float3 hitSpecularDirection = reflect(reflectionDir, hitSurface.normal);
        float hitNdotV = saturate(dot(hitSurface.normal, -reflectionDir));
        float2 hitBrdf = g_brdfLut.Sample(g_sampler, float2(hitNdotV, hitSurface.roughness)).rg;
        float3 hitEnvironmentSpecular =
            g_specularPrefilterMap.SampleLevel(g_sampler, hitSpecularDirection, specularMip).rgb;
        float3 specularIbl =
            EvaluatePbrSpecularIbl(
                hitEnvironmentSpecular, hitBrdf, hitF0, hitSurface.roughness, hitNdotV,
                hitSurface.ambientOcclusion) *
            iblIntensity *
            specularIblEnabled;
        float3 emissiveRadiance = hitSurface.emissive * emissiveEnabled;

        float3 component = directRadiance;
        if (debugTarget == 10)
        {
            component = diffuseIbl;
        }
        else if (debugTarget == 11)
        {
            component = specularIbl;
        }
        else if (debugTarget == 12)
        {
            component = emissiveRadiance;
        }

        component *= debugTarget == 12 ? 1.0 : (1.0 - hitSurface.unlit);
        component *= strength;
        return float4(component / (1.0 + component), 1.0);
    }

    float normalizedDistance = saturate(hitDistance / 50.0);
    return float4(normalizedDistance, normalizedDistance, normalizedDistance, 1.0);
}
