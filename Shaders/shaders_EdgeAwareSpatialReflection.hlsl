#include "FullscreenTriangle.hlsli"

Texture2D<float4> g_reflectionResolvedRadiance : register(t0, space9);
Texture2D<float4> g_reflectionRayHit : register(t0, space6);
Texture2D<float4> g_visibleNormal : register(t1, space3);
Texture2D<float4> g_visiblePbrParams : register(t4, space3);
Texture2D<float> g_visibleDepth : register(t6, space3);
Texture2D<float2> g_reflectionSpecularMoments : register(t0, space15);
Texture2D<float> g_reflectionSpecularConfidence : register(t0, space17);

cbuffer TemporalReflectionConstants : register(b5)
{
    uint g_historyValid;
    float g_historyWeight;
    uint g_frameIndex;
    float g_noiseStrength;
    uint g_rejectedPixelNeighborhoodEnabled;
    uint g_surfaceVarianceFilterEnabled;
    uint g_varianceGuidedTemporalEnabled;
    uint g_confidenceForceStableEvidence;
    uint g_spatiotemporalSpatialPolicyEnabled;
};

float2 DecodeOctahedral(float2 encoded)
{
    return encoded * 2.0 - 1.0;
}

float3 DecodeOctahedralNormal(float2 encoded)
{
    const float2 f = DecodeOctahedral(encoded);
    float3 normal = float3(f.x, f.y, 1.0 - abs(f.x) - abs(f.y));
    if (normal.z < 0.0)
    {
        const float2 octantSign = float2(normal.x >= 0.0 ? 1.0 : -1.0,
                                         normal.y >= 0.0 ? 1.0 : -1.0);
        normal.xy = (1.0 - abs(normal.yx)) * octantSign;
    }
    return normalize(normal);
}

FullscreenVSOutput VSMain(uint vertexId : SV_VertexID)
{
    return FullscreenTriangleVS(vertexId);
}

float4 PSMain(FullscreenVSOutput input) : SV_Target
{
    const int2 pixel = int2(input.position.xy);
    uint width;
    uint height;
    g_reflectionResolvedRadiance.GetDimensions(width, height);
    const int2 maxPixel = int2(width, height) - 1;

    const float4 centerRadiance = g_reflectionResolvedRadiance.Load(int3(pixel, 0));
    const float centerDepth = g_visibleDepth.Load(int3(pixel, 0));
    const float3 centerNormal = normalize(g_visibleNormal.Load(int3(pixel, 0)).xyz);
    const float centerRoughness = g_visiblePbrParams.Load(int3(pixel, 0)).y;
    const float4 centerHit = g_reflectionRayHit.Load(int3(pixel, 0));
    const bool centerHitValid = centerHit.y >= 0.5;

    if (centerRoughness <= 0.001)
    {
        return float4(centerRadiance.rgb, 1.0);
    }

    float3 radianceSum = 0.0;
    float weightSum = 0.0;
    for (int y = -1; y <= 1; ++y)
    {
        for (int x = -1; x <= 1; ++x)
        {
            const int2 samplePixel = clamp(pixel + int2(x, y), int2(0, 0), maxPixel);
            const float sampleDepth = g_visibleDepth.Load(int3(samplePixel, 0));
            const float3 sampleNormal = normalize(g_visibleNormal.Load(int3(samplePixel, 0)).xyz);
            const float sampleRoughness = g_visiblePbrParams.Load(int3(samplePixel, 0)).y;
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
                const float3 centerHitNormal = DecodeOctahedralNormal(centerHit.zw);
                const float3 sampleHitNormal = DecodeOctahedralNormal(sampleHit.zw);
                reflectionValid = distanceValid && dot(sampleHitNormal, centerHitNormal) >= 0.8;
            }

            if (depthValid && normalValid && roughnessValid && reflectionValid)
            {
                const float kernelWeight = x == 0 && y == 0 ? 4.0 : (x == 0 || y == 0 ? 2.0 : 1.0);
                radianceSum += g_reflectionResolvedRadiance.Load(int3(samplePixel, 0)).rgb * kernelWeight;
                weightSum += kernelWeight;
            }
        }
    }

    const float3 filtered = weightSum > 0.0 ? radianceSum / weightSum : centerRadiance.rgb;
    float spatialStrength = 1.0;
    if (g_spatiotemporalSpatialPolicyEnabled != 0)
    {
        const float2 moments = g_reflectionSpecularMoments.Load(int3(pixel, 0));
        const float variance = max(moments.y - moments.x * moments.x, 0.0);
        const float relativeStandardDeviation = sqrt(variance) / max(abs(moments.x), 0.01);
        const float confidence = saturate(g_reflectionSpecularConfidence.Load(int3(pixel, 0)));
        const float confidenceWeight = smoothstep(0.5, 0.9, confidence);
        const float varianceWeight = smoothstep(0.15, 0.5, relativeStandardDeviation);
        const float roughnessWeight = smoothstep(0.05, 0.35, centerRoughness);
        spatialStrength = min(confidenceWeight * varianceWeight * roughnessWeight, 0.75);
    }

    return float4(lerp(centerRadiance.rgb, filtered, spatialStrength), 1.0);
}
