#include "NrdRelaxComposition.hlsli"

Texture2D<float4> g_diffuse : register(t0);
Texture2D<float4> g_specular : register(t1);
Texture2D<float4> g_diffuseFactor : register(t2);
Texture2D<float4> g_specularFactor : register(t3);
Texture2D<float4> g_stable : register(t4);
Texture2D<float4> g_background : register(t5);
Texture2D<float4> g_native : register(t6);
Texture2D<float4> g_admission : register(t7);
RWTexture2D<float4> g_sceneColor : register(u0);
RWTexture2D<float4> g_diagnostics : register(u1);
cbuffer Dimensions : register(b0)
{
    uint width;
    uint height;
    uint forceNative;
};

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= width || id.y >= height)
        return;
    uint2 p = id.xy;
    NrdCompositionResult result = ComposeNrdRadiance(g_diffuse[p].rgb, g_specular[p].rgb,
        g_diffuseFactor[p], g_specularFactor[p], g_stable[p].rgb, g_background[p].rgb,
        g_native[p], forceNative != 0 ? float4(0, 1, 0, 0) : g_admission[p]);
    g_sceneColor[p] = result.color;
    g_diagnostics[p] = result.diagnostics;
}
