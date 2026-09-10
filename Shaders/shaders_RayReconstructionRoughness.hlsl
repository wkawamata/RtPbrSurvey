#include "FullscreenTriangle.hlsli"

Texture2D<float4> g_pbrParams : register(t4, space3);

FullscreenVSOutput VSMain(uint vertexId : SV_VertexID)
{
    return FullscreenTriangleVS(vertexId);
}

float PSMain(FullscreenVSOutput input) : SV_Target0
{
    return saturate(g_pbrParams.Load(int3(input.position.xy, 0)).g);
}
