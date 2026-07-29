#include "Scene/SceneBuilder.h"

#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace
{

bool TestTextureAndMaterial()
{
    Engine::SceneBuilder builder;
    const std::array<uint8_t, 16> pixels = {
        255, 0,   0,   255,
        0,   255, 0,   255,
        0,   0,   255, 255,
        255, 255, 255, 255,
    };

    const uint32_t textureIndex = builder.AddTextureRGBA8(2, 2, pixels);
    const uint32_t materialIndex =
        builder.AddTexturedMaterial(textureIndex, {20.0f, 20.0f}, {0.25f, 0.5f});
    const Engine::SceneTexture& texture = builder.GetMesh().textures.at(textureIndex);
    const Engine::SceneMaterial& material = builder.GetMesh().materials.at(materialIndex);

    return textureIndex == 0 && texture.width == 2 && texture.height == 2 && texture.component == 4 &&
        texture.generateMipmaps && texture.colorSpace == Engine::TextureColorSpace::Srgb &&
        texture.pixels.size() == pixels.size() &&
        material.albedoTexIndex == static_cast<int>(textureIndex) && material.metallicFactor == 0.0f &&
        material.roughnessFactor == 1.0f && material.uvScale.x == 20.0f && material.uvScale.y == 20.0f &&
        material.uvOffset.x == 0.25f && material.uvOffset.y == 0.5f;
}

bool TestValidation()
{
    Engine::SceneBuilder builder;
    bool rejectedPixelCount = false;
    bool rejectedTextureIndex = false;
    try
    {
        const std::vector<uint8_t> pixels(15);
        builder.AddTextureRGBA8(2, 2, pixels);
    }
    catch (const std::invalid_argument&)
    {
        rejectedPixelCount = true;
    }

    try
    {
        builder.AddTexturedMaterial(0);
    }
    catch (const std::out_of_range&)
    {
        rejectedTextureIndex = true;
    }
    return rejectedPixelCount && rejectedTextureIndex && builder.GetMesh().textures.empty() &&
        builder.GetMesh().materials.empty();
}

} // namespace

int main()
{
    if (!TestTextureAndMaterial() || !TestValidation())
    {
        std::cerr << "SceneBuilder texture tests failed.\n";
        return 1;
    }
    return 0;
}
