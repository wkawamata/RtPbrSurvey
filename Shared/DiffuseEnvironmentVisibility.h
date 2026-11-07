#pragma once

#include <cmath>
#include <cstdint>

namespace RtPbrSurvey
{
struct DiffuseEnvironmentVisibilitySettings
{
    bool enabled = false;
    float strength = 1.0f;
    uint32_t sampleCount = 16;
    float normalBias = 0.001f;
    float rayTMin = 0.0001f;
    float traceDistance = 10000.0f;
    bool sampleRotationEnabled = true;
    bool spatialFilterEnabled = true;
    float filterPlaneDistance = 0.005f;
    float filterWorldRadius = 0.08f;
    bool forceMaskedGeometry = false;

    bool IsValid() const
    {
        return std::isfinite(strength) && strength >= 0 && strength <= 1 &&
            sampleCount >= 1 && sampleCount <= 64 && std::isfinite(normalBias) && normalBias >= 0 &&
            std::isfinite(rayTMin) && rayTMin >= 0 && std::isfinite(traceDistance) && traceDistance > rayTMin &&
            std::isfinite(filterPlaneDistance) && filterPlaneDistance > 0 &&
            std::isfinite(filterWorldRadius) && filterWorldRadius > 0;
    }
};
} // namespace RtPbrSurvey
