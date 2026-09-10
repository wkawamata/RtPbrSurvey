#include "FullscreenTriangle.hlsli"

Texture2D<float4> g_reflectionRayHit : register(t0, space6);

FullscreenVSOutput VSMain(uint vertexId : SV_VertexID)
{
    return FullscreenTriangleVS(vertexId);
}

float PSMain(FullscreenVSOutput input) : SV_Target0
{
    const float4 rayHit = g_reflectionRayHit.Load(int3(input.position.xy, 0));
    return rayHit.y > 0.0 ? max(rayHit.x, 0.0) : 0.0;
}
