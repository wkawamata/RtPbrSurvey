#include "FullscreenTriangle.hlsli"

Texture2D<float4> g_reflectionEvaluatedRadiance : register(t0, space9);
Texture2D<float4> g_reflectionResolvedRadianceHistory : register(t0, space11);
Texture2D<float> g_reflectionHistoryDepth : register(t0, space12);
Texture2D<float4> g_reflectionHistoryNormal : register(t0, space13);
Texture2D<float4> g_visibleNormal : register(t1, space3);
Texture2D<float2> g_motionVector : register(t3, space3);
Texture2D<float4> g_visiblePbrParams : register(t4, space3);
Texture2D<float> g_visibleDepth : register(t6, space3);
Texture2D<float4> g_reflectionSpecularEstimate : register(t0, space16);
Texture2D<float4> g_reflectionResolvedSpecularEstimateHistory : register(t0, space14);
Texture2D<float2> g_reflectionSpecularMomentsHistory : register(t0, space15);

cbuffer CameraConstants : register(b0)
{
    float4x4 g_viewProj;
    float4x4 g_prevViewProj;
    float4x4 g_invViewProj;
    float3 g_cameraPosition;
    float g_cameraPad;
    float2 g_motionVectorValueOffset;
    float2 g_motionVectorJitterCancellationNdc;
};

cbuffer TemporalReflectionConstants : register(b5)
{
    uint g_historyValid;
    float g_historyWeight;
    uint g_frameIndex;
    float g_noiseStrength;
    uint g_rejectedPixelNeighborhoodEnabled;
    uint g_surfaceVarianceFilterEnabled;
};

uint HashTemporalNoise(uint2 pixel, uint frameIndex)
{
    uint value = pixel.x * 0x8da6b343u ^ pixel.y * 0xd8163841u ^ frameIndex * 0xcb1ab31fu;
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    return value ^ (value >> 16);
}

float3 FilterRejectedCurrentRadiance(int2 pixel, float currentDepth, float3 currentNormal, uint width, uint height)
{
    float3 radianceSum = 0.0;
    float weightSum = 0.0;
    const int2 maxPixel = int2(width, height) - 1;
    for (int y = -1; y <= 1; ++y)
    {
        for (int x = -1; x <= 1; ++x)
        {
            const int2 samplePixel = clamp(pixel + int2(x, y), int2(0, 0), maxPixel);
            const float sampleDepth = g_visibleDepth.Load(int3(samplePixel, 0));
            const float3 sampleNormal = normalize(g_visibleNormal.Load(int3(samplePixel, 0)).xyz);
            if (abs(sampleDepth - currentDepth) <= 0.002 && dot(sampleNormal, currentNormal) >= 0.9)
            {
                const float weight = x == 0 && y == 0 ? 2.0 : 1.0;
                float3 sampleRadiance = g_reflectionEvaluatedRadiance.Load(int3(samplePixel, 0)).rgb;
                const float unitNoise =
                    float(HashTemporalNoise(uint2(samplePixel), g_frameIndex)) / 4294967295.0;
                sampleRadiance *= 1.0 + (unitNoise * 2.0 - 1.0) * g_noiseStrength;
                radianceSum += sampleRadiance * weight;
                weightSum += weight;
            }
        }
    }
    return weightSum > 0.0 ? radianceSum / weightSum : g_reflectionEvaluatedRadiance.Load(int3(pixel, 0)).rgb;
}

float3 FilterSurfaceVariance(int2 pixel,
                             float currentDepth,
                             float3 currentNormal,
                             float2 currentMaterial,
                             uint width,
                             uint height)
{
    if (currentMaterial.y <= 0.001)
    {
        return g_reflectionEvaluatedRadiance.Load(int3(pixel, 0)).rgb;
    }

    float3 radianceSum = 0.0;
    float weightSum = 0.0;
    const int2 maxPixel = int2(width, height) - 1;
    for (int y = -1; y <= 1; ++y)
    {
        for (int x = -1; x <= 1; ++x)
        {
            const int2 samplePixel = clamp(pixel + int2(x, y), int2(0, 0), maxPixel);
            const float sampleDepth = g_visibleDepth.Load(int3(samplePixel, 0));
            const float3 sampleNormal = normalize(g_visibleNormal.Load(int3(samplePixel, 0)).xyz);
            const float2 sampleMaterial = g_visiblePbrParams.Load(int3(samplePixel, 0)).rg;
            const bool depthValid = abs(sampleDepth - currentDepth) <= 0.002;
            const bool normalValid = dot(sampleNormal, currentNormal) >= 0.95;
            const bool materialValid = abs(sampleMaterial.x - currentMaterial.x) <= 0.1 &&
                                       abs(sampleMaterial.y - currentMaterial.y) <= 0.1;
            if (depthValid && normalValid && materialValid)
            {
                const float weight = x == 0 && y == 0 ? 2.0 : 1.0;
                float3 sampleRadiance = g_reflectionEvaluatedRadiance.Load(int3(samplePixel, 0)).rgb;
                const float unitNoise =
                    float(HashTemporalNoise(uint2(samplePixel), g_frameIndex)) / 4294967295.0;
                sampleRadiance *= 1.0 + (unitNoise * 2.0 - 1.0) * g_noiseStrength;
                radianceSum += sampleRadiance * weight;
                weightSum += weight;
            }
        }
    }
    return weightSum > 0.0 ? radianceSum / weightSum :
                             g_reflectionEvaluatedRadiance.Load(int3(pixel, 0)).rgb;
}

FullscreenVSOutput VSMain(uint vertexId : SV_VertexID)
{
    return FullscreenTriangleVS(vertexId);
}

struct TemporalReflectionOutput
{
    float4 radiance : SV_Target0;
    float depth : SV_Target1;
    float4 normal : SV_Target2;
    float4 resolvedSpecularEstimate : SV_Target3;
    float2 specularMoments : SV_Target4;
};

TemporalReflectionOutput PSMain(FullscreenVSOutput input)
{
    const int3 pixel = int3(input.position.xy, 0);
    float4 current = g_reflectionEvaluatedRadiance.Load(pixel);
    float4 currentSpecularEstimate = g_reflectionSpecularEstimate.Load(pixel);
    const float currentSpecularLuminance = dot(
        max(currentSpecularEstimate.rgb, 0.0), float3(0.2126, 0.7152, 0.0722));
    float2 currentSpecularMoments =
        float2(currentSpecularLuminance, currentSpecularLuminance * currentSpecularLuminance);
    const float unitNoise = float(HashTemporalNoise(uint2(pixel.xy), g_frameIndex)) / 4294967295.0;
    current.rgb *= 1.0 + (unitNoise * 2.0 - 1.0) * g_noiseStrength;
    const float currentDepth = g_visibleDepth.Load(pixel);
    const float3 currentNormal = normalize(g_visibleNormal.Load(pixel).xyz);
    if (g_surfaceVarianceFilterEnabled != 0)
    {
        uint width;
        uint height;
        g_reflectionEvaluatedRadiance.GetDimensions(width, height);
        const float2 currentMaterial = g_visiblePbrParams.Load(pixel).rg;
        current.rgb = FilterSurfaceVariance(pixel.xy, currentDepth, currentNormal, currentMaterial, width, height);
    }
    bool historyRejected = false;
    // Alpha is diagnostic metadata only. RGB keeps the unweighted resolved-radiance contract.
    // 0.0: no history, 0.25: outside history, 0.5: depth reject, 0.75: normal reject, 1.0: accepted.
    current.a = 0.0;
    if (g_historyValid != 0)
    {
        uint width;
        uint height;
        g_reflectionEvaluatedRadiance.GetDimensions(width, height);

        const float2 storedMotionNdc = g_motionVector.Load(pixel);
        const float2 rawMotionNdc = storedMotionNdc - g_motionVectorJitterCancellationNdc -
                                    g_motionVectorValueOffset;
        const float2 currentUv = input.position.xy / float2(width, height);
        const float2 historyUv = currentUv + float2(0.5, -0.5) * rawMotionNdc;
        if (all(historyUv >= 0.0) && all(historyUv < 1.0))
        {
            current.a = 0.5;
            const int2 historyPixel = int2(historyUv * float2(width, height));
            const float2 currentNdc = float2(currentUv.x * 2.0 - 1.0, 1.0 - currentUv.y * 2.0);
            float4 currentWorld = mul(float4(currentNdc, currentDepth, 1.0), g_invViewProj);
            currentWorld /= currentWorld.w;
            const float4 previousClip = mul(currentWorld, g_prevViewProj);
            const float expectedPreviousDepth = previousClip.z / previousClip.w;
            const float previousDepth = g_reflectionHistoryDepth.Load(int3(historyPixel, 0));
            const float3 previousNormal = normalize(g_reflectionHistoryNormal.Load(int3(historyPixel, 0)).xyz);
            const bool depthValid = abs(previousDepth - expectedPreviousDepth) <= 0.002;
            const bool normalValid = dot(currentNormal, previousNormal) >= 0.9;
            historyRejected = !depthValid || !normalValid;
            if (depthValid)
            {
                current.a = 0.75;
                if (normalValid)
                {
                    const float4 history = g_reflectionResolvedRadianceHistory.Load(int3(historyPixel, 0));
                    current.rgb = lerp(current.rgb, history.rgb, g_historyWeight);
                    const float4 specularHistory =
                        g_reflectionResolvedSpecularEstimateHistory.Load(int3(historyPixel, 0));
                    const float2 momentsHistory =
                        g_reflectionSpecularMomentsHistory.Load(int3(historyPixel, 0));
                    currentSpecularEstimate.rgb =
                        lerp(currentSpecularEstimate.rgb, specularHistory.rgb, g_historyWeight);
                    currentSpecularMoments =
                        lerp(currentSpecularMoments, momentsHistory, g_historyWeight);
                    current.a = 1.0;
                }
            }
        }
        else
        {
            current.a = 0.25;
        }
    }
    if (historyRejected && g_rejectedPixelNeighborhoodEnabled != 0)
    {
        uint width;
        uint height;
        g_reflectionEvaluatedRadiance.GetDimensions(width, height);
        current.rgb = FilterRejectedCurrentRadiance(pixel.xy, currentDepth, currentNormal, width, height);
    }
    TemporalReflectionOutput output;
    output.radiance = current;
    output.depth = currentDepth;
    output.normal = float4(currentNormal, 1.0);
    output.resolvedSpecularEstimate = float4(currentSpecularEstimate.rgb, 1.0);
    output.specularMoments = currentSpecularMoments;
    return output;
}
