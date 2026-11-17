// A separate raster mask keeps selection acceptance independent of shading passes.
#define PSMain GBufferPSMain
#include "shaders_GBuffer.hlsl"
#undef PSMain

GBufferOutput PSMain(PSInput input, bool frontFace : SV_IsFrontFace)
{
    Material material = g_materialData[input.materialId];
    float2 uv = input.uv * material.uvScale + material.uvOffset;
    float metallic = saturate(material.metallicFactor *
        g_texture[NonUniformResourceIndex(material.metallicRoughnessTexIndex)].SampleLevel(g_sampler, uv, 0).b);
    float transmission = saturate(material.transmissionFactor *
        g_texture[NonUniformResourceIndex(material.transmissionTexIndex)].SampleLevel(g_sampler, uv, 0).r);
    bool allowBackFace = (material.flags & MaterialFlagVolumeTransmission) != 0 ||
        ((material.flags & MaterialFlagThinTransmission) != 0 && transmission * (1.0 - metallic) > 0.0);
    if (!frontFace && (material.flags & MaterialFlagDoubleSided) == 0 && !allowBackFace)
    {
        discard;
    }
    if ((material.flags & MaterialFlagAlphaMask) != 0)
    {
        float alpha = g_texture[NonUniformResourceIndex(material.albedoTexIndex)].SampleLevel(g_sampler, uv, 0).a * material.baseColorFactor.a;
        clip(alpha - material.alphaCutoff);
    }
    return GBufferPSMain(input);
}
