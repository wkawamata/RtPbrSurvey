#include "Scene/CameraView.h"

#include <DirectXMath.h>
#include <cmath>
#include <iostream>

namespace
{

bool IsFinite(DirectX::FXMMATRIX matrix)
{
    DirectX::XMFLOAT4X4 value;
    DirectX::XMStoreFloat4x4(&value, matrix);
    const float* elements = &value._11;
    for (size_t index = 0; index < 16; ++index)
    {
        if (!std::isfinite(elements[index]))
        {
            return false;
        }
    }
    return true;
}

bool NearlyEqual(float lhs, float rhs, float epsilon = 0.0001f)
{
    return std::abs(lhs - rhs) <= epsilon;
}

bool TestExactTopDownView()
{
    Engine::CameraState camera;
    camera.pos = {0.0f, 20.0f, 0.0f};
    camera.gazePoint = {0.0f, 0.0f, 0.0f};
    camera.up = {0.0f, 0.0f, 1.0f};
    camera.projection = Engine::CameraProjection::Orthographic;

    const Engine::CameraBasis basis = Engine::ResolveCameraBasis(camera);
    DirectX::XMFLOAT3 forward;
    DirectX::XMFLOAT3 right;
    DirectX::XMFLOAT3 up;
    DirectX::XMStoreFloat3(&forward, basis.forward);
    DirectX::XMStoreFloat3(&right, basis.right);
    DirectX::XMStoreFloat3(&up, basis.up);

    return IsFinite(Engine::CreateCameraViewMatrix(camera)) && NearlyEqual(forward.y, -1.0f) &&
        NearlyEqual(right.x, 1.0f) && NearlyEqual(up.z, 1.0f);
}

bool TestLegacyWorldUpCompatibility()
{
    Engine::CameraState camera;
    const DirectX::XMMATRIX actual = Engine::CreateCameraViewMatrix(camera);
    const DirectX::XMMATRIX expected =
        DirectX::XMMatrixLookAtLH(DirectX::XMLoadFloat3(&camera.pos),
                                  DirectX::XMLoadFloat3(&camera.gazePoint),
                                  DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f));

    DirectX::XMFLOAT4X4 actualValue;
    DirectX::XMFLOAT4X4 expectedValue;
    DirectX::XMStoreFloat4x4(&actualValue, actual);
    DirectX::XMStoreFloat4x4(&expectedValue, expected);
    const float* actualElements = &actualValue._11;
    const float* expectedElements = &expectedValue._11;
    for (size_t index = 0; index < 16; ++index)
    {
        if (!NearlyEqual(actualElements[index], expectedElements[index]))
        {
            return false;
        }
    }
    return true;
}

bool TestDegenerateInputFallback()
{
    Engine::CameraState camera;
    camera.gazePoint = camera.pos;
    camera.up = {0.0f, 0.0f, 0.0f};
    return IsFinite(Engine::CreateCameraViewMatrix(camera));
}

} // namespace

int main()
{
    if (!TestExactTopDownView() || !TestLegacyWorldUpCompatibility() || !TestDegenerateInputFallback())
    {
        std::cerr << "Camera view tests failed.\n";
        return 1;
    }
    return 0;
}
