#include "../Shaders/NrdRelaxInputConversion.hlsli"

struct ConversionResult
{
    float4 diffuse;
    float4 specular;
    float4 diffuseFactorStatus;
    float4 specularFactorStatus;
};
RWStructuredBuffer<ConversionResult> g_results : register(u0);

#ifdef NRD_CAPTURED_INPUT
struct CapturedInput
{
    float4 normalRoughness;
    float4 viewMetallic;
    float4 baseColor;
    float4 f0;
    float4 physicalDiffuse;
    float4 physicalSpecular;
    float4 distancesPrimary;
};
StructuredBuffer<CapturedInput> g_inputs : register(t0);
cbuffer ConversionCount : register(b0)
{
    uint g_inputCount;
};
#endif

[numthreads(64, 1, 1)] void CSMain(uint3 id : SV_DispatchThreadID)
{
#ifdef NRD_CAPTURED_INPUT
    if (id.x >= g_inputCount)
    {
        return;
    }
    CapturedInput input = g_inputs[id.x];
    if (input.distancesPrimary.z == 0)
    {
        ConversionResult excluded = (ConversionResult)0;
        excluded.diffuseFactorStatus.w = 4;
        excluded.specularFactorStatus.w = 4;
        g_results[id.x] = excluded;
        return;
    }
    NrdRelaxConvertedSignals converted = ConvertNrdRelaxSignals(input.normalRoughness.xyz,
                                                                input.viewMetallic.xyz,
                                                                input.baseColor.xyz,
                                                                input.viewMetallic.w,
                                                                input.f0.xyz,
                                                                input.normalRoughness.w,
                                                                input.physicalDiffuse.xyz,
                                                                input.physicalSpecular.xyz,
                                                                input.distancesPrimary.x,
                                                                input.distancesPrimary.y);
#else
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
    float3 normal = float3(0, 0, 1);
    float3 view = float3(sqrt(max(0, 1 - noV * noV)), 0, noV);
    float3 physicalDiffuse = baseColor * (1 - metallic) * float3(.25, 2, 6);
    float3 physicalSpecular = float3(.5, 1, 3);
    bool diffuseActive = metallic < 1 && any(baseColor > 0);
    float diffuseDistance = diffuseActive ? 2 : 0;
    if (index == 4)
    {
        physicalDiffuse = 60000;
    }
    if (index == 5)
    {
        normal.x = asfloat(0x7fc00000);
    }
    if (index == 6)
    {
        physicalSpecular.x = -1;
    }
    if (index == 7)
    {
        diffuseDistance = asfloat(0x7fc00000);
    }
    NrdRelaxConvertedSignals converted = ConvertNrdRelaxSignals(normal,
                                                                view,
                                                                baseColor,
                                                                metallic,
                                                                lerp(.04.xxx, baseColor, metallic),
                                                                roughness,
                                                                physicalDiffuse,
                                                                physicalSpecular,
                                                                diffuseDistance,
                                                                4);
#endif
    ConversionResult result;
    result.diffuse = converted.diffuse;
    result.specular = converted.specular;
    result.diffuseFactorStatus = float4(converted.diffuseFactor, converted.diffuseStatus);
    result.specularFactorStatus = float4(converted.specularFactor, converted.specularStatus);
    g_results[id.x] = result;
}
