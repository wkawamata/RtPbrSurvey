#include "../Shaders/PathTracingSampling.hlsli"

struct SamplingResult
{
    float4 halfVectorAndDistribution;
    float4 reflectedAndPdf;
    float4 brdfAndWeight;
    float4 normalAndValid;
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
    const float alphas[7] = {0.000001, 0.00001, 0.0000556767, 0.001, 0.01, 0.1, 1.0};
    const float alpha = alphas[caseIndex / 4];
    const float3 normals[4] = {float3(0, 0, 1), float3(1, 0, 0),
                              float3(0, 1, 0), float3(0.36, 0.48, 0.8)};
    const float3 normal = normalize(normals[caseIndex % 4]);
    const float3 halfVector = SamplePathTracingGgxHalfVectorWithAlpha(sample, alpha, normal);
    const float3 reflected = reflect(-normal, halfVector);
    const float distribution = PathTracingGgxDistribution(normal, halfVector, alpha);
    SamplingResult result;
    result.halfVectorAndDistribution = float4(halfVector, distribution);
    result.reflectedAndPdf = float4(reflected, distribution * 0.25);
    const float roughness = sqrt(alpha);
    const PathTracingBsdfSample bsdf = SamplePathTracingBsdf(0.8, 1.0, roughness, normal, normal, 0.0, sample);
    result.brdfAndWeight = float4(bsdf.weight, bsdf.pdf);
    result.normalAndValid = float4(normal, bsdf.valid);
    g_results[id.x] = result;
}
