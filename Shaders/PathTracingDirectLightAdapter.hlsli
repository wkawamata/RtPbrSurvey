#ifndef RTPBRSURVEY_PATH_TRACING_DIRECT_LIGHT_ADAPTER_HLSLI
#define RTPBRSURVEY_PATH_TRACING_DIRECT_LIGHT_ADAPTER_HLSLI

#include "DirectLightEvaluation.hlsli"

// Matches the first eight float4 registers and light array of LightingConstants (b2).
cbuffer PathTracingLights : register(b2)
{
    float4 ptLightHeader[7];
    float2 ptLightTail;
    uint ptLightCount;
    uint ptPrimaryShadowLightIndex;
    LightGpuData ptLights[16];
};

PathTracingLightSample MakePathTracingSceneLightSample(uint index, float3 worldPosition, float rayTMin, float rayTMax)
{
    PathTracingLightSample invalid = MakeInvalidPathTracingLightSample();
    if (index >= min(ptLightCount, 16u) || ptLights[index].enabled == 0u)
    {
        return invalid;
    }
    DirectLightSample direct = EvaluateDirectLightData(ptLights[index], worldPosition);
    if (!all(isfinite(direct.radiance)) || !any(direct.radiance > 0.0) ||
        !isfinite(direct.distance) || direct.distance <= 0.0)
    {
        return invalid;
    }
    const float distance = ptLights[index].type == 0u ? rayTMax : min(direct.distance, rayTMax);
    if (distance <= rayTMin)
    {
        return invalid;
    }
    return MakePathTracingDirectionalLightSample(
        direct.surfaceToLight, direct.radiance, distance, 1.0, index);
}

void ResolvePathTracingShadowRay(PathTracingLightSample sample,
                                 float3 shadowOrigin,
                                 inout float3 direction,
                                 inout float distance)
{
    if (sample.isDelta == 0u || sample.sourceIndex >= min(ptLightCount, 16u) ||
        ptLights[sample.sourceIndex].type == 0u)
    {
        return;
    }
    const float3 lightOffset = ptLights[sample.sourceIndex].position - shadowOrigin;
    const float distanceToLight = length(lightOffset);
    if (distanceToLight <= 0.0)
    {
        distance = 0.0;
        return;
    }
    direction = lightOffset / distanceToLight;
    distance = min(sample.distance, distanceToLight);
}

#endif
