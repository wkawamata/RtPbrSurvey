#include "FullscreenTriangle.hlsli"

Texture2D<float> g_visibility : register(t0, space4);

FullscreenVSOutput VSMain(uint vertexId : SV_VertexID)
{
    return FullscreenTriangleVS(vertexId);
}

float4 PSMain(FullscreenVSOutput input) : SV_TARGET
{
    uint width, height;
    g_visibility.GetDimensions(width, height);
    uint2 pixel = min(uint2(input.uv * float2(width, height)), uint2(width - 1, height - 1));
    float v = g_visibility.Load(int3(pixel, 0));
    return v < 0 ? float4(1, 0, 1, 1) : float4(v, v, v, 1);
}
