#pragma once

#include "Scene.h"

#include <algorithm>

namespace Engine
{

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
