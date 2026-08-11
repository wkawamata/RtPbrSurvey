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

} // namespace Engine
