#pragma once

#include <cstdint>

namespace Engine
{

static constexpr uint32_t kMaterialCount = 256;
static constexpr uint32_t kMaterialFlagUnlit = 1u << 0;
static constexpr uint32_t kMaterialFlagHasNormalTexture = 1u << 1;

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
};

} // namespace Engine
