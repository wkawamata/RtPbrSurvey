#include "stdafx.h"

#include "TextureMipChain.h"

#include <algorithm>
#include <cmath>

namespace Engine
{
namespace
{

float SrgbToLinear(uint8_t value)
{
    const float srgb = static_cast<float>(value) / 255.0f;
    return srgb <= 0.04045f ? srgb / 12.92f : std::pow((srgb + 0.055f) / 1.055f, 2.4f);
}

uint8_t LinearToSrgb(float value)
{
    value = std::clamp(value, 0.0f, 1.0f);
    const float srgb = value <= 0.0031308f ? value * 12.92f : 1.055f * std::pow(value, 1.0f / 2.4f) - 0.055f;
    return static_cast<uint8_t>(std::lround(std::clamp(srgb, 0.0f, 1.0f) * 255.0f));
}

Rgba8MipLevel Downsample(const Rgba8MipLevel& source, TextureColorSpace colorSpace)
{
    Rgba8MipLevel result;
    result.width = (std::max)(1u, source.width / 2);
    result.height = (std::max)(1u, source.height / 2);
    result.pixels.resize(static_cast<size_t>(result.width) * result.height * 4);

    for (uint32_t y = 0; y < result.height; ++y)
    {
        for (uint32_t x = 0; x < result.width; ++x)
        {
            float linearRgb[3] = {};
            float alpha = 0.0f;
            uint32_t sampleCount = 0;
            const uint32_t sourceYBegin = y * source.height / result.height;
            const uint32_t sourceYEnd = (y + 1) * source.height / result.height;
            const uint32_t sourceXBegin = x * source.width / result.width;
            const uint32_t sourceXEnd = (x + 1) * source.width / result.width;
            for (uint32_t sourceY = sourceYBegin; sourceY < sourceYEnd; ++sourceY)
            {
                for (uint32_t sourceX = sourceXBegin; sourceX < sourceXEnd; ++sourceX)
                {
                    const size_t sourceOffset =
                        (static_cast<size_t>(sourceY) * source.width + sourceX) * 4;
                    for (size_t channel = 0; channel < 3; ++channel)
                    {
                        const uint8_t value = source.pixels[sourceOffset + channel];
                        linearRgb[channel] += colorSpace == TextureColorSpace::Srgb
                            ? SrgbToLinear(value)
                            : static_cast<float>(value) / 255.0f;
                    }
                    alpha += static_cast<float>(source.pixels[sourceOffset + 3]) / 255.0f;
                    ++sampleCount;
                }
            }

            const float sampleScale = 1.0f / static_cast<float>(sampleCount);
            const size_t resultOffset = (static_cast<size_t>(y) * result.width + x) * 4;
            for (size_t channel = 0; channel < 3; ++channel)
            {
                const float value = linearRgb[channel] * sampleScale;
                result.pixels[resultOffset + channel] = colorSpace == TextureColorSpace::Srgb
                    ? LinearToSrgb(value)
                    : static_cast<uint8_t>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
            }
            result.pixels[resultOffset + 3] =
                static_cast<uint8_t>(std::lround(std::clamp(alpha * sampleScale, 0.0f, 1.0f) * 255.0f));
        }
    }
    return result;
}

} // namespace

std::vector<Rgba8MipLevel> GenerateRgba8MipChain(std::span<const uint8_t> basePixels,
                                                 uint32_t width,
                                                 uint32_t height,
                                                 bool generateMipmaps,
                                                 TextureColorSpace colorSpace)
{
    Rgba8MipLevel base;
    base.width = width;
    base.height = height;
    base.pixels.assign(basePixels.begin(), basePixels.end());

    std::vector<Rgba8MipLevel> levels;
    levels.push_back(std::move(base));
    while (generateMipmaps && (levels.back().width > 1 || levels.back().height > 1))
    {
        levels.push_back(Downsample(levels.back(), colorSpace));
    }
    return levels;
}

} // namespace Engine
