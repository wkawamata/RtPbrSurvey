#include "../Shaders/NrdRelaxInputAdmission.hlsli"

struct AdmissionInput
{
    float4 diffuse;
    float4 specular;
    float4 diffuseState;
    float4 specularState;
    float4 validity;
    float4 viewZ;
    float4 reserved;
};
StructuredBuffer<AdmissionInput> g_inputs : register(t0);
RWStructuredBuffer<NrdAdmissionResult> g_results : register(u0);
cbuffer InputCount : register(b0)
{
    uint count;
};
[numthreads(64, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= count)
        return;
    AdmissionInput input = g_inputs[id.x];
    g_results[id.x] = AdmitNrdReconstruction(input.diffuse, input.specular, input.diffuseState,
        input.specularState, input.validity, input.viewZ);
}
