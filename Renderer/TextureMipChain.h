#pragma once

#include "Scene/Scene.h"

#include <cstdint>
#include <span>
#include <vector>

namespace Engine
{

struct Rgba8MipLevel
{
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint8_t> pixels;
};

std::vector<Rgba8MipLevel> GenerateRgba8MipChain(std::span<const uint8_t> basePixels,
                                                 uint32_t width,
                                                 uint32_t height,
                                                 bool generateMipmaps,
                                                 TextureColorSpace colorSpace);

} // namespace Engine
