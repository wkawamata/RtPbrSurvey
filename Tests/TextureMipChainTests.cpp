#include "Renderer/TextureMipChain.h"

#include <array>
#include <cstdint>
#include <iostream>

namespace
{

bool TestGeneratedMipChain()
{
    const std::array<uint8_t, 32> pixels = {
        0,   0,   0,   255, 255, 255, 255, 255, 0,   0,   0,   255, 255, 255, 255, 255,
        255, 255, 255, 255, 0,   0,   0,   255, 255, 255, 255, 255, 0,   0,   0,   255,
    };
    const std::vector<Engine::Rgba8MipLevel> levels =
        Engine::GenerateRgba8MipChain(pixels, 4, 2, true, Engine::TextureColorSpace::Srgb);
    return levels.size() == 3 && levels[0].width == 4 && levels[0].height == 2 && levels[1].width == 2 &&
        levels[1].height == 1 && levels[2].width == 1 && levels[2].height == 1 &&
        levels[2].pixels.size() == 4 && levels[2].pixels[0] > 180 && levels[2].pixels[0] < 195;
}

bool TestSingleMip()
{
    const std::array<uint8_t, 4> pixels = {1, 2, 3, 4};
    const std::vector<Engine::Rgba8MipLevel> levels =
        Engine::GenerateRgba8MipChain(pixels, 1, 1, false, Engine::TextureColorSpace::Srgb);
    return levels.size() == 1 && levels[0].pixels[0] == 1 && levels[0].pixels[3] == 4;
}

bool TestOddDimensionsIncludeEdgeTexels()
{
    const std::array<uint8_t, 12> pixels = {
        0, 0, 0, 255,
        0, 0, 0, 255,
        255, 255, 255, 255,
    };
    const std::vector<Engine::Rgba8MipLevel> levels =
        Engine::GenerateRgba8MipChain(pixels, 3, 1, true, Engine::TextureColorSpace::Srgb);
    return levels.size() == 2 && levels[1].width == 1 && levels[1].height == 1 && levels[1].pixels[0] > 150;
}

bool TestLinearDataMip()
{
    const std::array<uint8_t, 8> pixels = {
        0, 0, 0, 255,
        255, 255, 255, 255,
    };
    const std::vector<Engine::Rgba8MipLevel> levels =
        Engine::GenerateRgba8MipChain(pixels, 2, 1, true, Engine::TextureColorSpace::Linear);
    return levels.size() == 2 && levels[1].pixels[0] >= 127 && levels[1].pixels[0] <= 128;
}

} // namespace

int main()
{
    if (!TestGeneratedMipChain() || !TestSingleMip() || !TestOddDimensionsIncludeEdgeTexels() ||
        !TestLinearDataMip())
    {
        std::cerr << "Texture mip-chain tests failed.\n";
        return 1;
    }
    return 0;
}
