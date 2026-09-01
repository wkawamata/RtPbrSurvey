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
Texture2D<float4> g_reflectionEvaluatedRadiance : register(t0, space9);
Texture2D<float4> g_reflectionRayEmission : register(t0, space10);
Texture2D<float2> g_reflectionSpecularMoments : register(t0, space15);
Texture2D<float> g_reflectionSpecularConfidence : register(t0, space17);
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

PbrSurface MakeReflectionHitSurface(float3 albedo, float4 material, float3 normal, float3 emissive)
{
    return MakePbrSurface(albedo, normal, emissive, material.x, material.y, 1.0, material.z);
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
        float3 radiance = g_reflectionEvaluatedRadiance.Sample(g_sampler, input.uv).rgb;
        return float4(radiance / (1.0 + radiance), 1.0);
    }

    if (debugTarget == 8)
    {
        return float4(rayEmission / (1.0 + rayEmission), 1.0);
    }

    if (debugTarget == 13)
    {
        const float classification = g_reflectionEvaluatedRadiance.Sample(g_sampler, input.uv).a;
        if (classification < 0.125)
        {
            return float4(0.0, 0.0, 0.0, 1.0); // No history.
        }
        if (classification < 0.375)
        {
            return float4(0.0, 0.25, 1.0, 1.0); // Reprojected outside history.
        }
        if (classification < 0.625)
        {
            return float4(1.0, 0.0, 0.0, 1.0); // Depth rejection.
        }
        if (classification < 0.875)
        {
            return float4(1.0, 0.8, 0.0, 1.0); // Normal rejection.
        }
        return float4(0.0, 0.8, 0.2, 1.0); // History accepted.
    }

    if (debugTarget == 14)
    {
        float3 resolvedEstimate = g_reflectionEvaluatedRadiance.Sample(g_sampler, input.uv).rgb;
        return float4(resolvedEstimate / (1.0 + resolvedEstimate), 1.0);
    }

    if (debugTarget == 15)
    {
        float2 moments = g_reflectionEvaluatedRadiance.Sample(g_sampler, input.uv).rg;
        float variance = max(moments.y - moments.x * moments.x, 0.0);
        float mappedVariance = variance / (1.0 + variance);
        return float4(mappedVariance.xxx, 1.0);
    }

    if (debugTarget == 16)
    {
        float confidence = g_reflectionEvaluatedRadiance.Sample(g_sampler, input.uv).r;
        return float4(confidence.xxx, 1.0);
    }

    if (debugTarget == 17)
    {
        const int2 pixel = int2(input.position.xy);
        uint width;
        uint height;
        g_reflectionSpecularMoments.GetDimensions(width, height);
        const int2 maxPixel = int2(width, height) - 1;
        const float2 moments = g_reflectionSpecularMoments.Load(int3(pixel, 0));
        const float variance = max(moments.y - moments.x * moments.x, 0.0);
        const float mappedVariance = variance / (1.0 + variance);
        const float confidence = saturate(g_reflectionSpecularConfidence.Load(int3(pixel, 0)));
        const float centerDepth = g_depth.Load(int3(pixel, 0));
        const float3 centerNormal = normalize(g_normal.Load(int3(pixel, 0)).xyz);
        const float centerRoughness = g_pbrParams.Load(int3(pixel, 0)).y;
        const float4 centerHit = g_reflectionRayHit.Load(int3(pixel, 0));
        const bool centerHitValid = centerHit.y >= 0.5;
        float acceptedNeighborCount = 0.0;

        for (int y = -1; y <= 1; ++y)
        {
            for (int x = -1; x <= 1; ++x)
            {
                if (x == 0 && y == 0)
                {
                    continue;
                }

                const int2 samplePixel = clamp(pixel + int2(x, y), int2(0, 0), maxPixel);
                const float sampleDepth = g_depth.Load(int3(samplePixel, 0));
                const float3 sampleNormal = normalize(g_normal.Load(int3(samplePixel, 0)).xyz);
                const float sampleRoughness = g_pbrParams.Load(int3(samplePixel, 0)).y;
                const float4 sampleHit = g_reflectionRayHit.Load(int3(samplePixel, 0));
                const bool sampleHitValid = sampleHit.y >= 0.5;
                const bool depthValid = abs(sampleDepth - centerDepth) <= 0.002;
                const bool normalValid = dot(sampleNormal, centerNormal) >= 0.95;
                const bool roughnessValid = abs(sampleRoughness - centerRoughness) <= 0.1;
                bool reflectionValid = sampleHitValid == centerHitValid;
                if (reflectionValid && centerHitValid)
                {
                    const float distanceThreshold = max(0.05, centerHit.x * 0.1);
                    const bool distanceValid = abs(sampleHit.x - centerHit.x) <= distanceThreshold;
                    const float3 centerHitNormal = DecodeNormalOctahedron(centerHit.zw);
                    const float3 sampleHitNormal = DecodeNormalOctahedron(sampleHit.zw);
                    reflectionValid = distanceValid && dot(sampleHitNormal, centerHitNormal) >= 0.8;
                }

                acceptedNeighborCount += depthValid && normalValid && roughnessValid && reflectionValid ? 1.0 : 0.0;
            }
        }

        return float4(confidence, mappedVariance, acceptedNeighborCount / 8.0, 1.0);
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
        float3 hitViewDir = -reflectionDir;
        float3 diffuseIrradiance = g_diffuseIrradianceMap.Sample(g_sampler, hitSurface.normal).rgb;
        float specularMip = hitSurface.roughness * SPECULAR_PREFILTER_MAX_MIP;
        float3 hitSpecularDirection = reflect(reflectionDir, hitSurface.normal);
        float hitNdotV = saturate(dot(hitSurface.normal, -reflectionDir));
        float2 hitBrdf = g_brdfLut.Sample(g_sampler, float2(hitNdotV, hitSurface.roughness)).rg;
        float3 hitEnvironmentSpecular =
            g_specularPrefilterMap.SampleLevel(g_sampler, hitSpecularDirection, specularMip).rgb;
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

        float3 component = hitRadiance.direct;
        if (debugTarget == 10)
        {
            component = hitRadiance.diffuseIbl;
        }
        else if (debugTarget == 11)
        {
            component = hitRadiance.specularIbl;
        }
        else if (debugTarget == 12)
        {
            component = hitRadiance.emissive * emissiveEnabled;
        }

        component *= debugTarget == 12 ? 1.0 : (1.0 - hitSurface.unlit);
        return float4(component / (1.0 + component), 1.0);
    }

    float normalizedDistance = saturate(hitDistance / 50.0);
    return float4(normalizedDistance, normalizedDistance, normalizedDistance, 1.0);
}
