#include "../Shaders/RayReconstructionInputConversion.hlsli"

RWStructuredBuffer<RrConvertedInputs> g_results : register(u0);

[numthreads(64, 1, 1)] void CSMain(uint3 id : SV_DispatchThreadID)
{
    uint test = id.x % 32;
    if (test >= 16)
    {
        float4 ray = float4(2, 2, 1, 0);
        float4 surface = float4(2, 1, 0, 0);
        float4 normal = float4(0, 0, 1, .5);
        float4 base = float4(.7, .5, .3, 1);
        float4 material = float4(.04, .04, .04, 0);
        float4 primary = float4(0, 0, 2, 1);
        float3 radiance = float3(.25, 2, 70000);
        float2 motion = float2(.2, -.4);
        float depth = 2;
        if (test == 17 || test == 19)
        {
            ray.z = 2;
            surface = float4(0, 2, 0, 0);
        }
        if (test == 18 || test == 19)
            ray.y = 1;
        if (test == 20 || test == 21 || test == 26 || test == 27)
        {
            ray = 0;
            surface = 0;
        }
        if (test == 21)
            ray.x = 1;
        if (test == 22 || test == 23)
        {
            ray = surface = primary = base = material = 0;
            normal = float4(0, 0, 0, 1);
            depth = 0;
        }
        if (test == 23)
            motion.x = asfloat(0x7fc00000);
        if (test == 24)
            material.w = 2;
        if (test == 25)
            normal.z = 0;
        if (test == 26)
            radiance.x = -1;
        if (test == 27)
            surface.y = 1;
        if (test == 28)
            surface.y = 2;
        if (test == 29)
            ray.y = 3;
        if (test == 30)
            normal.w = 1;
        if (test == 31)
            material.w = .5;
        g_results[id.x] = ConvertRrLiveInputs(
            radiance, normal, depth, motion, base, material, primary, ray, surface, float3(0, 0, 1));
        return;
    }
    float4 ray = float4(1.999, 2, 1, 0);
    float4 surface = float4(2, 1, 0, 0);
    float4 normal = float4(0, 0, 1, 0);
    float4 material = float4(.7, .5, .3, 1);
    float4 primary = float4(0, 0, 2, 1);
    float3 color = float3(.25, 2, 70000);
    bool allowMiss = true;
    if (test == 1 || test == 6 || test == 12)
    {
        ray = float4(100, 2, 2, 0);
        surface = float4(0, 2, 0, 0);
    }
    if (test == 2)
    {
        ray = 0;
        surface = 0;
    }
    if (test == 3)
        ray.y = 1;
    if (test == 4)
        surface.w = 1;
    if (test == 5)
        surface.x = 0;
    if (test == 6)
        surface.x = 1;
    if (test == 7)
        ray.x = asfloat(0x7fc00000);
    if (test == 8)
        normal.w = .3;
    if (test == 9)
        material.w = .5;
    if (test == 10)
        normal.xyz = 0;
    if (test == 11)
        primary.w = 0;
    if (test == 12)
        allowMiss = false;
    if (test == 13)
    {
        ray.z = 3;
        surface.y = 3;
    }
    if (test == 14)
    {
        ray.x = 69999;
        surface.x = 70000;
    }
    if (test == 15)
        color.x = -1;
    float4 distance = RrActualSpecularDistance(ray, surface, material.w, normal.w);
    g_results[id.x] = ConvertRrInputs(color,
                                      normal,
                                      2,
                                      float2(.2, -.4),
                                      float4(.7, .5, .3, 1),
                                      material,
                                      primary,
                                      distance,
                                      float3(0, 0, 1),
                                      allowMiss);
}
