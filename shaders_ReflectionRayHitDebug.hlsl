#include "FullscreenTriangle.hlsli"

Texture2D<float2> g_reflectionRayHit : register(t0, space4);
SamplerState g_sampler : register(s0);

cbuffer ReflectionRayHitDebugConstants : register(b1)
{
    uint debugTarget;
};

FullscreenVSOutput VSMain(uint vertexId : SV_VertexID)
{
    return FullscreenTriangleVS(vertexId);
}

float4 PSMain(FullscreenVSOutput input) : SV_TARGET
{
    float2 rayHit = g_reflectionRayHit.Sample(g_sampler, input.uv);
    float hitDistance = rayHit.x;
    float hitFlag = rayHit.y;

    if (debugTarget == 0)
    {
        return float4(hitFlag, hitFlag, hitFlag, 1.0);
    }

    float normalizedDistance = saturate(hitDistance / 50.0);
    return float4(normalizedDistance, normalizedDistance, normalizedDistance, 1.0);
}
