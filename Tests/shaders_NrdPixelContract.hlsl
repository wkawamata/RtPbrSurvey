#include "../Shaders/NrdRelaxInputConversion.hlsli"

struct ContractInput
{
    float4 diffuseFactor;
    float4 specularFactor;
    float4 physicalDiffuse;
    float4 physicalSpecular;
    float4 distancesPrimary;
    float4 active;
    float4 reserved;
};
struct ContractOutput
{
    float4 diffuse;
    float4 specular;
    float4 diffuseFactorStatus;
    float4 specularFactorStatus;
};
StructuredBuffer<ContractInput> g_inputs : register(t0);
RWStructuredBuffer<ContractOutput> g_results : register(u0);
cbuffer ConversionCount : register(b0)
{
    uint g_inputCount;
};

[numthreads(64, 1, 1)] void CSMain(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= g_inputCount)
    {
        return;
    }
    ContractInput input = g_inputs[id.x];
    ContractOutput result = (ContractOutput)0;
    if (input.distancesPrimary.z == 0)
    {
        uint status = any(input.physicalDiffuse.xyz != 0) || any(input.physicalSpecular.xyz != 0) ? 1 : 4;
        result.diffuseFactorStatus.w = status;
        result.specularFactorStatus.w = status;
    }
    else
    {
        uint diffuseStatus = InspectNrdGpuSignal(
            input.physicalDiffuse.xyz, input.diffuseFactor.xyz, input.distancesPrimary.x, input.active.x != 0);
        uint specularStatus = InspectNrdGpuSignal(
            input.physicalSpecular.xyz, input.specularFactor.xyz, input.distancesPrimary.y, input.active.y != 0);
        result.diffuseFactorStatus = float4(input.diffuseFactor.xyz, diffuseStatus);
        result.specularFactorStatus = float4(input.specularFactor.xyz, specularStatus);
        if ((diffuseStatus == 0 || diffuseStatus == 3) && (specularStatus == 0 || specularStatus == 3))
        {
            if (diffuseStatus == 0)
            {
                result.diffuse = RELAX_FrontEnd_PackRadianceAndHitDist(
                    input.physicalDiffuse.xyz / input.diffuseFactor.xyz, input.distancesPrimary.x, true);
            }
            if (specularStatus == 0)
            {
                result.specular = RELAX_FrontEnd_PackRadianceAndHitDist(
                    input.physicalSpecular.xyz / input.specularFactor.xyz, input.distancesPrimary.y, true);
            }
        }
    }
    g_results[id.x] = result;
}
