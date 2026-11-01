#pragma once

#include <cstddef>
#include <limits>
#include <type_traits>

namespace Engine
{
struct VolumeAbsorptionParameters
{
    float attenuationColor[3] = {1.0f, 1.0f, 1.0f};
    float attenuationDistance = std::numeric_limits<float>::infinity();
};
static_assert(std::is_standard_layout_v<VolumeAbsorptionParameters>);
static_assert(sizeof(VolumeAbsorptionParameters) == 16);
static_assert(offsetof(VolumeAbsorptionParameters, attenuationColor) == 0);
static_assert(offsetof(VolumeAbsorptionParameters, attenuationDistance) == 12);
}
