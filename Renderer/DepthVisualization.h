#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace Engine
{
enum class DepthVisualizationMode : uint32_t
{
    RawDevice,
    LinearView,
    LogView,
};

struct DepthVisualizationShaderConstants
{
    uint32_t mode;
    float displayNear;
    float displayFar;
    float gamma;
    uint32_t invert;
    float cameraNear;
    float cameraFar;
    uint32_t orthographicProjection;
};

struct DepthVisualizationSettings
{
    DepthVisualizationMode mode = DepthVisualizationMode::LogView;
    float displayNear = 0.1f;
    float displayFar = 100.0f;
    float gamma = 1.0f;
    bool invert = false;

    static DepthVisualizationSettings CreateDefault(float cameraNear, float cameraFar)
    {
        DepthVisualizationSettings settings;
        settings.Reset(cameraNear, cameraFar);
        return settings;
    }

    void Reset(float cameraNear, float cameraFar)
    {
        const float sanitizedNear = SanitizePositive(cameraNear, 0.1f);
        const float sanitizedFar = (std::max)(SanitizePositive(cameraFar, 100.0f), sanitizedNear + 0.001f);
        mode = DepthVisualizationMode::LogView;
        displayNear = sanitizedNear;
        displayFar = (std::min)(sanitizedFar, (std::max)(100.0f, sanitizedNear + 1.0f));
        gamma = 1.0f;
        invert = false;
    }

    DepthVisualizationShaderConstants MakeShaderConstants(
        float cameraNear, float cameraFar, bool orthographicProjection) const
    {
        const float sanitizedCameraNear = SanitizePositive(cameraNear, 0.1f);
        const float sanitizedCameraFar =
            (std::max)(SanitizePositive(cameraFar, 100.0f), sanitizedCameraNear + 0.001f);
        const float sanitizedDisplayNear = SanitizePositive(displayNear, sanitizedCameraNear);
        const float sanitizedDisplayFar =
            (std::max)(SanitizePositive(displayFar, sanitizedCameraFar), sanitizedDisplayNear + 0.001f);
        const float sanitizedGamma = SanitizePositive(gamma, 1.0f);
        return {static_cast<uint32_t>(mode),
                sanitizedDisplayNear,
                sanitizedDisplayFar,
                sanitizedGamma,
                invert ? 1u : 0u,
                sanitizedCameraNear,
                sanitizedCameraFar,
                orthographicProjection ? 1u : 0u};
    }

private:
    static float SanitizePositive(float value, float fallback)
    {
        return std::isfinite(value) && value > 0.0f ? value : fallback;
    }
};

static_assert(sizeof(DepthVisualizationShaderConstants) == 8 * sizeof(uint32_t));
} // namespace Engine
