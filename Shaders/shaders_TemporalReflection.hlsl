#include "FullscreenTriangle.hlsli"

Texture2D<float4> g_reflectionEvaluatedRadiance : register(t0, space9);
Texture2D<float4> g_reflectionResolvedRadianceHistory : register(t0, space11);

cbuffer TemporalReflectionConstants : register(b5)
{
    uint g_historyValid;
    float g_historyWeight;
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
        // Deliberately unreprojected bootstrap blend. It exists to expose temporal stability
        // and ghosting before motion reprojection and rejection are introduced.
        current.rgb = lerp(current.rgb, history.rgb, g_historyWeight);
    }
    return current;
}
