#include "NRD.hlsli"

struct MaterialResult
{
    float4 diffuseFactor;
    float4 specularFactor;
    float4 recoveredDiffuse;
    float4 recoveredSpecular;
};
RWStructuredBuffer<MaterialResult> g_results : register(u0);

[numthreads(64, 1, 1)] void CSMain(uint3 id : SV_DispatchThreadID)
{
    uint index = id.x % 8;
    float3 baseColor = float3(.8, .6, .2);
    float metallic = 0;
    float roughness = .5;
    float noV = 1;
    if (index == 1)
    {
        baseColor = 0;
        noV = .8;
    }
    if (index == 2)
    {
        baseColor = float3(.9, .5, .1);
        metallic = 1;
        roughness = .2;
        noV = .3;
    }
    if (index == 3)
    {
        baseColor = 0;
        metallic = 1;
        roughness = .25;
        noV = .001;
    }
    if (index == 4)
    {
        baseColor = float3(.7, .3, .1);
        metallic = .35;
        roughness = .55;
        noV = .75;
    }
    if (index == 5)
    {
        baseColor = float3(.4, .6, .8);
        roughness = 0;
        noV = .5;
    }
    if (index == 6)
    {
        baseColor = 1;
        roughness = 1;
    }
    if (index == 7)
    {
        baseColor = 1;
        noV = 0;
    }
    const float3 diffuseReflectance = baseColor * (1 - metallic);
    const float3 f0 = lerp(.04.xxx, baseColor, metallic);
    const float3 normal = float3(0, 0, 1);
    const float3 view = float3(sqrt(max(0, 1 - noV * noV)), 0, noV);
    float3 diffuseFactor, specularFactor;
    NRD_MaterialFactors(normal, view, diffuseReflectance, f0, roughness, diffuseFactor, specularFactor);
    const float3 physicalDiffuse = diffuseReflectance * float3(.25, 2, 6);
    const float3 physicalSpecular = float3(.5, 1, 3);
    const float4 packedDiffuse = RELAX_FrontEnd_PackRadianceAndHitDist(physicalDiffuse / diffuseFactor, 2, true);
    const float4 packedSpecular = RELAX_FrontEnd_PackRadianceAndHitDist(physicalSpecular / specularFactor, 4, true);
    MaterialResult result;
    result.diffuseFactor = float4(diffuseFactor, 0);
    result.specularFactor = float4(specularFactor, 0);
    result.recoveredDiffuse = float4(packedDiffuse.xyz * diffuseFactor, packedDiffuse.w);
    result.recoveredSpecular = float4(packedSpecular.xyz * specularFactor, packedSpecular.w);
    g_results[id.x] = result;
}
