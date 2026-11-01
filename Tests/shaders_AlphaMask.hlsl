#include "../Shaders/AlphaMaskCoverage.hlsli"
#include "../Shaders/EmissiveTriangleSampling.hlsli"

struct CoverageResult
{
    float4 coverage;
    float4 reserved0;
    float4 reserved1;
    float4 reserved2;
};
RWStructuredBuffer<CoverageResult> g_results : register(u0);

[numthreads(64, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= 28 * 1024)
    {
        return;
    }
    const uint test = id.x / 1024;
    const float factors[7] = {0.0, 0.1, 0.25, 0.5, 0.75, 0.9, 1.0};
    const float cutoffs[4] = {0.0, 0.25, 0.5, 1.1};
    const float alphas[4] = {0.0, 0.25, 0.5, 1.0};
    const float alpha = alphas[id.x % 4];
    const float factor = factors[test / 4];
    const float cutoff = cutoffs[test % 4];
    CoverageResult result = (CoverageResult)0;
    result.coverage = float4(PassAlphaMaskCoverage(alpha, factor, cutoff) ? 1.0 : 0.0, alpha * factor, cutoff, factor);
    EmissiveTriangleGpu emitter = (EmissiveTriangleGpu)0;
    emitter.position1 = float3(1, 0, 0);
    emitter.position2 = float3(0, 1, 0);
    emitter.area = 0.5;
    emitter.selectionPdf = 1;
    const float3 samplePosition = float3(0.25, 0.25, 0);
    result.reserved0 = float4(
        EmissiveTriangleSolidAnglePdf(emitter, float3(0.25, 0.25, 2), samplePosition, false),
        EmissiveTriangleSolidAnglePdf(emitter, float3(0.25, 0.25, -2), samplePosition, false),
        EmissiveTriangleSolidAnglePdf(emitter, float3(0.25, 0.25, 2), samplePosition, true),
        EmissiveTriangleSolidAnglePdf(emitter, float3(0.25, 0.25, -2), samplePosition, true));
    g_results[id.x] = result;
}
