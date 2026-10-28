#include "../Shaders/RayReconstructionInputConversion.hlsli"

RWStructuredBuffer<RrConvertedInputs> g_results : register(u0);

[numthreads(64, 1, 1)] void CSMain(uint3 id : SV_DispatchThreadID)
{
    uint test = id.x % 32;
    float3 color = float3(.25, 3, 70000);
    float4 normal = float4(0, 0, 1, .5);
    float depth = 12;
    float2 motion = float2(.2, -.4);
    float4 albedo = float4(.8, .6, .2, 1);
    float4 material = float4(.04, .04, .04, 0);
    float4 primary = float4(1, 2, 3, 1);
    float4 distance = float4(2, 1, 1.999, 0);
    float3 view = float3(0, 0, 1);
    if (test == 1)
    {
        material = float4(.9, .5, .1, 1);
        albedo.xyz = material.xyz;
        normal.w = .2;
        view = float3(.8, 0, .6);
    }
    if (test == 2)
    {
        material = float4(.271, .131, .061, .35);
        albedo.xyz = float3(.7, .3, .1);
        normal.w = .55;
    }
    if (test == 3)
    {
        material = float4(0, 0, 0, 1);
        albedo.xyz = 0;
        view = float3(1, 0, 0);
    }
    if (test == 4)
    {
        normal.w = 0;
        view.z = -1;
    }
    if (test == 5)
        normal.w = 1;
    if (test == 6)
        view = float3(1, 0, 0);
    if (test == 7)
    {
        normal.z = 1.001;
        distance.x = 70000;
        distance.z = 69999;
    }
    if (test == 8)
        primary.w = 0;
    if (test == 9)
        albedo.w = 0;
    if (test == 10)
        material.x = -1;
    if (test == 11)
        material.w = 1.1;
    if (test == 12)
        normal.w = -1;
    if (test == 13)
        albedo.x = 1.1;
    if (test == 14)
        normal.xyz = 0;
    if (test == 15)
        view = 0;
    if (test == 16)
        depth = 0;
    if (test == 17)
        motion.x = asfloat(0x7fc00000);
    if (test == 18)
        color.y = -1;
    if (test == 19)
        color.z = asfloat(0x7f800000);
    if (test == 20)
        distance = float4(0, 2, 100, 0);
    if (test == 21)
        distance = 0;
    if (test == 22)
        distance = float4(0, 3, 0, 0);
    if (test == 23)
        distance = float4(0, 4, 0, 0);
    if (test == 24)
        distance.x = -1;
    if (test == 25)
        distance.z = 0;
    if (test == 26)
        distance.w = 1;
    if (test == 27)
        distance.x = asfloat(0x7fc00000);
    if (test == 28)
        normal.w = asfloat(0x7fc00000);
    if (test == 29)
        primary.x = asfloat(0x7f800000);
    if (test == 30)
        material.z = asfloat(0x7fc00000);
    if (test == 31)
        depth = asfloat(0x7f800000);
    g_results[id.x] = ConvertRrInputs(color, normal, depth, motion, albedo, material, primary, distance, view);
}
