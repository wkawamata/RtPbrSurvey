#include "FullscreenTriangle.hlsli"

Texture2D<float4> g_reflectionEvaluatedRadiance : register(t0, space9);
Texture2D<float4> g_reflectionResolvedRadianceHistory : register(t0, space11);

cbuffer TemporalReflectionConstants : register(b5)
{
    uint g_historyValid;
};

FullscreenVSOutput VSMain(uint vertexId : SV_VertexID)
{
    return FullscreenTriangleVS(vertexId);
}

float4 PSMain(FullscreenVSOutput input) : SV_TARGET
{
    const int3 pixel = int3(input.position.xy, 0);
    float4 current = g_reflectionEvaluatedRadiance.Load(pixel);
    if (g_historyValid != 0)
    {
        const float4 history = g_reflectionResolvedRadianceHistory.Load(pixel);
        // Identity phase: consume the valid history input without changing current-frame RGB.
        // Both resources carry opaque linear HDR radiance, so alpha remains one by contract.
        current.a = min(current.a, history.a);
    }
    return current;
}
