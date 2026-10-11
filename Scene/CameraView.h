#pragma once

#include "Scene.h"

#include <cmath>

namespace Engine
{

struct CameraBasis
{
    DirectX::XMVECTOR forward;
    DirectX::XMVECTOR right;
    DirectX::XMVECTOR up;
};

inline CameraBasis ResolveCameraBasis(const CameraState& camera)
{
    using namespace DirectX;

    const XMVECTOR eye = XMLoadFloat3(&camera.pos);
    XMVECTOR forward = XMVectorSubtract(XMLoadFloat3(&camera.gazePoint), eye);
    if (XMVectorGetX(XMVector3LengthSq(forward)) < 1.0e-8f)
    {
        forward = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);
    }
    forward = XMVector3Normalize(forward);

    XMVECTOR requestedUp = XMLoadFloat3(&camera.up);
    if (XMVectorGetX(XMVector3LengthSq(requestedUp)) < 1.0e-8f)
    {
        requestedUp = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    }
    requestedUp = XMVector3Normalize(requestedUp);

    XMVECTOR right = XMVector3Cross(requestedUp, forward);
    if (XMVectorGetX(XMVector3LengthSq(right)) < 1.0e-8f)
    {
        const float forwardY = std::abs(XMVectorGetY(forward));
        requestedUp = forwardY < 0.999f ? XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f)
                                        : XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);
        right = XMVector3Cross(requestedUp, forward);
    }
    right = XMVector3Normalize(right);

    return {forward, right, XMVector3Normalize(XMVector3Cross(forward, right))};
}

inline DirectX::XMMATRIX CreateCameraViewMatrix(const CameraState& camera)
{
    const CameraBasis basis = ResolveCameraBasis(camera);
    return DirectX::XMMatrixLookToLH(DirectX::XMLoadFloat3(&camera.pos), basis.forward, basis.up);
}

inline DirectX::XMFLOAT3 GetCameraRotationRadians(const CameraState& camera)
{
    const CameraBasis basis = ResolveCameraBasis(camera);
    DirectX::XMFLOAT3 forward, right, up;
    DirectX::XMStoreFloat3(&forward, basis.forward);
    DirectX::XMStoreFloat3(&right, basis.right);
    DirectX::XMStoreFloat3(&up, basis.up);
    const float horizontal = std::sqrt(forward.x * forward.x + forward.z * forward.z);
    if (horizontal < 1.0e-5f)
    {
        return {std::atan2(-forward.y, horizontal), std::atan2(-right.z, right.x), 0.0f};
    }
    return {std::atan2(-forward.y, horizontal), std::atan2(forward.x, forward.z),
            std::atan2(right.y, up.y)};
}

inline void SetCameraRotationRadians(CameraState& camera, const DirectX::XMFLOAT3& rotation)
{
    using namespace DirectX;
    const XMMATRIX matrix = XMMatrixRotationRollPitchYaw(rotation.x, rotation.y, rotation.z);
    const float distance = XMVectorGetX(XMVector3Length(
        XMVectorSubtract(XMLoadFloat3(&camera.gazePoint), XMLoadFloat3(&camera.pos))));
    const XMVECTOR forward = XMVector3TransformNormal(XMVectorSet(0, 0, 1, 0), matrix);
    XMStoreFloat3(&camera.gazePoint, XMVectorAdd(XMLoadFloat3(&camera.pos),
        XMVectorScale(forward, distance > 1.0e-4f ? distance : 1.0f)));
    XMStoreFloat3(&camera.up, XMVector3TransformNormal(XMVectorSet(0, 1, 0, 0), matrix));
    camera.rot = rotation;
}

inline void SetCameraPosition(CameraState& camera, const DirectX::XMFLOAT3& position)
{
    using namespace DirectX;
    const XMVECTOR delta = XMVectorSubtract(XMLoadFloat3(&position), XMLoadFloat3(&camera.pos));
    XMStoreFloat3(&camera.gazePoint, XMVectorAdd(XMLoadFloat3(&camera.gazePoint), delta));
    camera.pos = position;
}

inline bool CameraViewParametersMatch(const CameraState& left, const CameraState& right)
{
    const auto close = [](float a, float b) { return std::abs(a - b) <= 0.0001f; };
    const CameraBasis leftBasis = ResolveCameraBasis(left);
    const CameraBasis rightBasis = ResolveCameraBasis(right);
    const auto sameDirection = [](DirectX::FXMVECTOR a, DirectX::FXMVECTOR b)
    {
        return DirectX::XMVectorGetX(DirectX::XMVector3Length(DirectX::XMVectorSubtract(a, b))) <= 0.00001f;
    };
    return close(left.pos.x, right.pos.x) && close(left.pos.y, right.pos.y) && close(left.pos.z, right.pos.z) &&
        sameDirection(leftBasis.forward, rightBasis.forward) && sameDirection(leftBasis.up, rightBasis.up) &&
        left.projection == right.projection && close(left.fov, right.fov) &&
        close(left.orthographicHeight, right.orthographicHeight) && close(left.nearZ, right.nearZ) &&
        close(left.farZ, right.farZ) && close(left.lensShiftX, right.lensShiftX) && close(left.lensShiftY, right.lensShiftY);
}

} // namespace Engine
