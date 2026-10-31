#include "NrdRelaxInputAdmission.hlsli"

Texture2D<float4> g_diffuse : register(t0);
Texture2D<float4> g_specular : register(t1);
Texture2D<float4> g_diffuseState : register(t2);
Texture2D<float4> g_specularState : register(t3);
Texture2D<float4> g_validity : register(t4);
Texture2D<float4> g_viewZ : register(t5);
RWTexture2D<float4> g_outputDiffuse : register(u0);
RWTexture2D<float4> g_outputSpecular : register(u1);
RWTexture2D<float4> g_outputViewZ : register(u2);
RWTexture2D<float4> g_admission : register(u3);
cbuffer Constants : register(b0)
{
    uint width;
    uint height;
};

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= width || id.y >= height)
        return;
    uint2 pixel = id.xy;
    NrdAdmissionResult result = AdmitNrdReconstruction(g_diffuse[pixel], g_specular[pixel],
        g_diffuseState[pixel], g_specularState[pixel], g_validity[pixel], g_viewZ[pixel]);
    g_outputDiffuse[pixel] = result.diffuse;
    g_outputSpecular[pixel] = result.specular;
    g_outputViewZ[pixel] = result.viewZ;
    g_admission[pixel] = result.admission;
}
