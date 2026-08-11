#pragma once

#include "Scene.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Engine
{

// Returns the perspective FOV Y in degrees that preserves the vertical framing
// of an orthographic height at the specified focus distance. Height and distance
// use the same world unit. Invalid inputs return quiet NaN.
inline float PerspectiveFovYFromOrthographicHeight(float orthographicHeight, float focusDistance)
{
    if (!std::isfinite(orthographicHeight) || orthographicHeight <= 0.0f || !std::isfinite(focusDistance) ||
        focusDistance <= 0.0f)
    {
        return std::numeric_limits<float>::quiet_NaN();
    }

    const double fovYRadians = 2.0 * std::atan(
        static_cast<double>(orthographicHeight) / (2.0 * static_cast<double>(focusDistance)));
    const double fovYDegrees = fovYRadians * (180.0 / static_cast<double>(DirectX::XM_PI));
    if (!std::isfinite(fovYDegrees) || fovYDegrees <= 0.0 || fovYDegrees >= 180.0)
    {
        return std::numeric_limits<float>::quiet_NaN();
    }
    const float result = static_cast<float>(fovYDegrees);
    return result < 180.0f ? result : std::nextafter(180.0f, 0.0f);
}

// Returns the orthographic vertical height that preserves a perspective FOV Y
// at the specified focus distance. FOV Y uses degrees; height and distance use
// the same world unit. Invalid inputs return quiet NaN.
inline float OrthographicHeightFromPerspectiveFovY(float fovYDegrees, float focusDistance)
{
    if (!std::isfinite(fovYDegrees) || fovYDegrees <= 0.0f || fovYDegrees >= 180.0f ||
        !std::isfinite(focusDistance) || focusDistance <= 0.0f)
    {
        return std::numeric_limits<float>::quiet_NaN();
    }

    const double fovYRadians = static_cast<double>(fovYDegrees) * (static_cast<double>(DirectX::XM_PI) / 180.0);
    const double orthographicHeight = 2.0 * static_cast<double>(focusDistance) * std::tan(fovYRadians * 0.5);
    const float result = static_cast<float>(orthographicHeight);
    if (!std::isfinite(result) || result <= 0.0f)
    {
        return std::numeric_limits<float>::quiet_NaN();
    }
    return result;
}

// Legacy compatibility wrapper. It intentionally preserves the original input
// and output clamps; new host code should prefer the strict named utilities.
inline float MatchPerspectiveToOrthographic(float orthographicHeight, float focusDistance)
{
    const float height = std::clamp(orthographicHeight, 0.001f, 1000000.0f);
    const float distance = std::clamp(focusDistance, 0.001f, 1000000.0f);
    return std::clamp(PerspectiveFovYFromOrthographicHeight(height, distance), 0.1f, 179.0f);
}

inline DirectX::XMMATRIX CreateCameraProjectionMatrix(const CameraState& camera, float aspectRatio)
{
    const float safeAspectRatio = (std::max)(aspectRatio, 0.0001f);
    const float nearZ = std::clamp(camera.nearZ, 0.001f, 100000.0f);
    const float farZ = std::clamp(camera.farZ, nearZ + 0.001f, 1000000.0f);

    if (camera.projection == CameraProjection::Orthographic)
    {
        const float height = std::clamp(camera.orthographicHeight, 0.001f, 1000000.0f);
        return DirectX::XMMatrixOrthographicLH(height * safeAspectRatio, height, nearZ, farZ);
    }

    const float fovYDegrees = std::clamp(camera.fov, 0.1f, 179.0f);
    return DirectX::XMMatrixPerspectiveFovLH(
        DirectX::XMConvertToRadians(fovYDegrees), safeAspectRatio, nearZ, farZ);
}

} // namespace Engine
