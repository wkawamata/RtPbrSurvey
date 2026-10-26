#include "VolumeAbsorptionParameters.hlsli"

struct Material
{
    uint albedoTexIndex;
    uint metallicRoughnessTexIndex;
    uint emissiveTexIndex;
    uint occlusionTexIndex;
    uint normalTexIndex;
    float roughnessFactor;
    float metallicFactor;
    float occlusionStrength;
    float ambientOcclusionFactor;
    float emissiveScale;
    uint flags;
    float2 uvScale;
    float2 uvOffset;
    float4 baseColorFactor;
    float3 emissiveFactor;
    float normalTextureScale;
    float transmissionFactor;
    float3 transmissionTint;
    float transmissionIor;
    uint transmissionTexIndex;
    uint transmissionTintTexIndex;
    float alphaCutoff;
    VolumeAbsorptionParameters volumeAbsorption;
};

static const uint MaterialFlagUnlit = 1u << 0;
static const uint MaterialFlagHasNormalTexture = 1u << 1;
static const uint MaterialFlagThinTransmission = 1u << 2;
static const uint MaterialFlagAlphaMask = 1u << 3;
static const uint MaterialFlagDoubleSided = 1u << 4;
static const uint MaterialFlagVolumeTransmission = 1u << 5;
