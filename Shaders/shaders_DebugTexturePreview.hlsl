#include "FullscreenTriangle.hlsli"

Texture2D<float4> g_source : register(t0, space4);
SamplerState g_sampler : register(s0);

cbuffer DebugTexturePreviewConstants : register(b3)
{
    uint semantic;
    uint channel;
    float exposure;
    float scale;
    float offset;
    uint nearestSampling;
};

FullscreenVSOutput VSMain(uint vertexId : SV_VertexID)
{
    return FullscreenTriangleVS(vertexId);
}

float4 LoadSource(float2 uv)
{
    if (nearestSampling == 0)
    {
        return g_source.Sample(g_sampler, uv);
    }

    uint width;
    uint height;
    g_source.GetDimensions(width, height);
    int2 pixel = min(int2(uv * float2(width, height)), int2(width - 1, height - 1));
    return g_source.Load(int3(pixel, 0));
}

float4 PSMain(FullscreenVSOutput input) : SV_TARGET
{
    float4 value = LoadSource(input.uv);
    if (semantic == 1)
    {
        value.rgb = value.rgb * 0.5 + 0.5;
    }
    else if (semantic == 3)
    {
        value.rgb = float3(value.xy * 0.5 + 0.5, 0.0);
    }

    value.rgb *= exp2(exposure);
    value = value * scale + offset;

    if (channel > 0)
    {
        const uint component = min(channel - 1, 3);
        value = value[component].xxxx;
    }
    return value;
}
