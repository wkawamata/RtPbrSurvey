#include "../Shaders/ThinTransmission.hlsli"

struct SamplingResult
{
    float4 fresnelAndDirection;
    float4 weightAndReflection;
    float4 reserved0;
    float4 reserved1;
};
RWStructuredBuffer<SamplingResult> g_results : register(u0);

[numthreads(64, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= 28 * 1024)
    {
        return;
    }
    const uint caseIndex = id.x / 1024;
    const float iors[7] = {1.0, 1.1, 1.33, 1.5, 2.0, 4.0, 100.0};
    const float cosines[4] = {1.0, 0.8, 0.1, 0.0};
    const float cosine = cosines[caseIndex % 4];
    const float ior = iors[caseIndex / 4];
    const float3 incident = float3(sqrt(max(0.0, 1.0 - cosine * cosine)), 0.0, -cosine);
    const ThinTransmissionSample sample = SampleThinTransmission(incident, float3(0.0, 0.0, 1.0),
        ior, float3(0.2, 0.4, 0.8), (float(id.x % 1024) + 0.5) / 1024.0);
    SamplingResult result;
    result.fresnelAndDirection = float4(ThinTransmissionFresnel(cosine, ior), sample.direction);
    result.weightAndReflection = float4(sample.weight, sample.reflected ? 1.0 : 0.0);
    result.reserved0 = 0.0;
    result.reserved1 = 0.0;
    g_results[id.x] = result;
}
