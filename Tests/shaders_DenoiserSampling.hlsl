#include "../Shaders/PathTracingDenoiserSampling.hlsli"

struct SamplingResult
{
    float4 halfVectorAndPdf;
    float4 directionAndValid;
    float4 diffuseAndPdf;
    float4 reflectionPdf;
};

RWStructuredBuffer<SamplingResult> g_results : register(u0);

[numthreads(64, 1, 1)] void CSMain(uint3 id : SV_DispatchThreadID)
{
    const uint caseIndex = id.x / 1024;
    if (caseIndex >= 28)
    {
        return;
    }
    const uint index = id.x % 1024;
    float2 sample = (float2(index % 32, index / 32) + 0.5) / 32.0;
    if (index == 0)
    {
        sample = 0.0;
    }
    if (index == 1023)
    {
        sample = 0.99999;
    }
    const float roughnesses[5] = {0.0, 0.1, 0.35, 0.7, 1.0};
    const float viewZs[4] = {1.0, 0.8, 0.1, 0.0001};
    float roughness = roughnesses[min(caseIndex / 4, 4)];
    const float viewZ = viewZs[caseIndex % 4];
    float3 view = float3(sqrt(max(0.0, 1.0 - viewZ * viewZ)), 0.0, viewZ);
    if (caseIndex == 20)
        roughness = -1.0;
    if (caseIndex == 21)
        roughness = 2.0;
    if (caseIndex == 22)
        roughness = asfloat(0x7fc00000);
    if (caseIndex == 23)
        view = float3(1.0, 0.0, 0.0);
    if (caseIndex == 24)
        view = float3(0.0, 0.0, -1.0);
    if (caseIndex == 25)
        view *= 2.0;
    if (caseIndex == 26)
        sample.x = 1.0;
    if (caseIndex == 27)
        sample.y = -0.1;
    const float3 halfVector = SampleDenoiserGgxVisibleNormal(sample, roughness, view);
    const float halfPdf = DenoiserGgxVisibleNormalPdf(halfVector, roughness, view);
    const float3 direction = halfPdf > 0.0 ? reflect(-view, halfVector) : float3(0.0, 0.0, 0.0);
    const bool valid = halfPdf > 0.0 && direction.z > 0.0;
    const float3 diffuse = SampleDenoiserCosineDirection(sample);
    SamplingResult result;
    result.halfVectorAndPdf = float4(halfVector, halfPdf);
    result.directionAndValid = float4(direction, valid ? 1.0 : 0.0);
    result.diffuseAndPdf = float4(diffuse, diffuse.z / kDenoiserSamplingPi);
    result.reflectionPdf = float4(valid ? halfPdf / (4.0 * dot(view, halfVector)) : 0.0, 0.0, 0.0, 0.0);
    g_results[id.x] = result;
}
