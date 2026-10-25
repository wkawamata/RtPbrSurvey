#include "../Shaders/ThinTransmission.hlsli"

struct SamplingResult
{
    float4 directionAndPdf;
    float4 weightAndReflection;
    float4 state;
    float4 bsdfAndCosine;
};
RWStructuredBuffer<SamplingResult> g_results : register(u0);

[numthreads(64, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= 28 * 1024)
    {
        return;
    }
    const uint test = id.x / 1024;
    const uint index = id.x % 1024;
    const float roughnesses[5] = {0.0, 0.1, 0.35, 0.7, 1.0};
    const float cosines[4] = {1.0, 0.8, 0.1, 0.0001};
    const float roughness = test < 20 ? roughnesses[test / 4] : 0.7;
    const float ior = test < 20 ? 1.5 : (test < 24 ? 1.0 : 1.33);
    const float cosine = cosines[test % 4];
    const float3 incident = float3(sqrt(max(0.0, 1.0 - cosine * cosine)), 0.0, -cosine);
    const float branchRandom = (float((index * 193u) % 1024u) + 0.5) / 1024.0;
    const float2 directionRandom = float2((float(index) + 0.5) / 1024.0,
        frac((float(index) + 0.5) * 0.61803398875));
    const ThinTransmissionSample sample = SampleRoughThinTransmission(incident, float3(0, 0, 1),
        roughness, ior, float3(0.2, 0.4, 0.8), branchRandom, directionRandom);
    SamplingResult result = (SamplingResult)0;
    result.directionAndPdf = float4(sample.direction, sample.pdf);
    result.weightAndReflection = float4(sample.weight, sample.reflected ? 1.0 : 0.0);
    result.state = float4(sample.valid ? 1.0 : 0.0, sample.delta ? 1.0 : 0.0, 0, 0);
    if (sample.valid && !sample.delta)
    {
        result.bsdfAndCosine.xyz = EvaluateRoughThinTransmission(-incident, sample.direction, float3(0, 0, 1),
            roughness, ior, float3(0.2, 0.4, 0.8), result.state.z);
        result.bsdfAndCosine.w = abs(sample.direction.z);
    }
    g_results[id.x] = result;
}
