#include "FullscreenTriangle.hlsli"

Texture2D<float4> g_reflectionRayHit : register(t0, space4);
SamplerState g_sampler : register(s0);

cbuffer ReflectionRayHitDebugConstants : register(b1)
{
    uint debugTarget;
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

FullscreenVSOutput VSMain(uint vertexId : SV_VertexID)
{
    return FullscreenTriangleVS(vertexId);
}

float4 PSMain(FullscreenVSOutput input) : SV_TARGET
{
    float4 rayHit = g_reflectionRayHit.Sample(g_sampler, input.uv);
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

    float normalizedDistance = saturate(hitDistance / 50.0);
    return float4(normalizedDistance, normalizedDistance, normalizedDistance, 1.0);
}
