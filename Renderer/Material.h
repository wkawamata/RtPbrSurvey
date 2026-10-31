#pragma once

#include <cstdint>
#include "../Shared/VolumeAbsorptionParameters.h"

namespace Engine
{

static constexpr uint32_t kMaterialCount = 256;
static constexpr uint32_t kMaterialFlagUnlit = 1u << 0;
static constexpr uint32_t kMaterialFlagHasNormalTexture = 1u << 1;
static constexpr uint32_t kMaterialFlagThinTransmission = 1u << 2;
static constexpr uint32_t kMaterialFlagAlphaMask = 1u << 3;
static constexpr uint32_t kMaterialFlagDoubleSided = 1u << 4;
static constexpr uint32_t kMaterialFlagVolumeTransmission = 1u << 5;

struct Material
{
    uint32_t albedoTexIndex;
    uint32_t metallicRoughnessTexIndex;
    uint32_t emissiveTexIndex;
    uint32_t occlusionTexIndex;
    uint32_t normalTexIndex;
    float roughnessFactor;
    float metallicFactor;
    float occlusionStrength;
    float ambientOcclusionFactor;
    float emissiveScale;
    uint32_t flags;
    float uvScale[2];
    float uvOffset[2];
    float baseColorFactor[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    float emissiveFactor[3];
    float normalTextureScale;
    float transmissionFactor = 0.0f;
    float transmissionTint[3] = {1.0f, 1.0f, 1.0f};
    float transmissionIor = 1.5f;
    uint32_t transmissionTexIndex = 0;
    uint32_t transmissionTintTexIndex = 0;
    float alphaCutoff = 0.5f;
    VolumeAbsorptionParameters volumeAbsorption;
};

} // namespace Engine
