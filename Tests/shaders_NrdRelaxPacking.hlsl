#include "NRD.hlsli"

struct PackingResult
{
    float4 packed;
    float4 reserved0;
    float4 reserved1;
    float4 reserved2;
};

RWStructuredBuffer<PackingResult> g_results : register(u0);

[numthreads(64, 1, 1)] void CSMain(uint3 id : SV_DispatchThreadID)
{
    const uint caseIndex = id.x % 8;
    float3 radiance = float3(1.0, 2.0, 3.0);
    float distance = 2.0;
    if (caseIndex == 1)
        distance = NRD_INF;
    if (caseIndex == 2)
        distance = 0.0;
    if (caseIndex == 3)
        distance = -1.0;
    if (caseIndex == 4)
        distance = asfloat(0x7fc00000);
    if (caseIndex == 5)
        radiance.x = asfloat(0x7f800000);
    if (caseIndex == 6)
        radiance = float3(-1.0, 70000.0, 4.0);
    if (caseIndex == 7)
        distance = asfloat(0x7f800000);
    PackingResult result;
    result.packed = RELAX_FrontEnd_PackRadianceAndHitDist(radiance, distance, true);
    result.reserved0 = 0.0;
    result.reserved1 = 0.0;
    result.reserved2 = 0.0;
    g_results[id.x] = result;
}
