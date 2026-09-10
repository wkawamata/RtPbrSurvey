#include "FullscreenTriangle.hlsli"
#include "PbrLighting.hlsli"

Texture2D<float4> g_albedo : register(t0, space3);
Texture2D<float4> g_pbrParams : register(t4, space3);

FullscreenVSOutput VSMain(uint vertexId : SV_VertexID)
{
    return FullscreenTriangleVS(vertexId);
}

float4 PSMain(FullscreenVSOutput input) : SV_Target0
{
    const int3 pixel = int3(input.position.xy, 0);
    const float3 albedo = saturate(g_albedo.Load(pixel).rgb);
    const float metallic = saturate(g_pbrParams.Load(pixel).r);
    return float4(PbrF0(albedo, metallic), 1.0);
}
