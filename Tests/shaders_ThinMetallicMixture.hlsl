#include "../Shaders/ThinTransmission.hlsli"

struct SamplingResult
{
    float4 selection;
    float4 reserved0;
    float4 reserved1;
    float4 reserved2;
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
    const float metallics[7] = {0.0, 0.1, 0.25, 0.5, 0.75, 0.9, 1.0};
    const float transmissions[4] = {0.0, 0.25, 0.5, 1.0};
    const float random = (float(id.x % 1024) + 0.5) / 1024.0;
    const ThinMetallicMixtureSample sample = SampleThinMetallicMixture(
        transmissions[test % 4], metallics[test / 4], random);
    SamplingResult result = (SamplingResult)0;
    result.selection = float4(sample.thin ? 1.0 : 0.0, sample.opaqueMetallic,
        sample.thin ? 0.9 : 0.2 + 0.4 * sample.opaqueMetallic, 0.0);
    const float roughness = transmissions[test % 4];
    const float alpha = max(roughness * roughness, 0.001);
    result.reserved0 = float4(PathTracingGgxDistribution(1.0, alpha), PathTracingGgxDistribution(0.5, alpha),
        PathTracingGgxDistribution(0.1, alpha),
        PathTracingSpecularPdf(float3(0, 0, 1), float3(0, 0, 1), float3(0, 0, 1), roughness));
    g_results[id.x] = result;
}
