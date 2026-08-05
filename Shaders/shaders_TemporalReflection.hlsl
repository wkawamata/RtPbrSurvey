#include "FullscreenTriangle.hlsli"

Texture2D<float4> g_reflectionEvaluatedRadiance : register(t0, space9);

FullscreenVSOutput VSMain(uint vertexId : SV_VertexID)
{
    return FullscreenTriangleVS(vertexId);
}

float4 PSMain(FullscreenVSOutput input) : SV_TARGET
{
    return g_reflectionEvaluatedRadiance.Load(int3(input.position.xy, 0));
}
