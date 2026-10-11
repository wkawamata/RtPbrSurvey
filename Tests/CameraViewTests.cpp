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

bool TestLiveCameraEdits()
{
    Engine::CameraState camera;
    const DirectX::XMFLOAT3 rotation = {0.35f, -0.7f, 0.45f};
    Engine::SetCameraRotationRadians(camera, rotation);
    const DirectX::XMFLOAT3 actual = Engine::GetCameraRotationRadians(camera);
    if (!NearlyEqual(actual.x, rotation.x) || !NearlyEqual(actual.y, rotation.y) ||
        !NearlyEqual(actual.z, rotation.z))
    {
        return false;
    }
    const Engine::CameraBasis before = Engine::ResolveCameraBasis(camera);
    Engine::SetCameraPosition(camera, {7.0f, 2.0f, -3.0f});
    const Engine::CameraBasis after = Engine::ResolveCameraBasis(camera);
    return DirectX::XMVectorGetX(DirectX::XMVector3Length(DirectX::XMVectorSubtract(
        before.forward, after.forward))) < 0.0001f &&
        DirectX::XMVectorGetX(DirectX::XMVector3Length(DirectX::XMVectorSubtract(
        before.up, after.up))) < 0.0001f;
}

bool TestRotationPoleRoundTrip()
{
    for (float pitch : {DirectX::XM_PIDIV2, -DirectX::XM_PIDIV2})
    {
        Engine::CameraState camera;
        Engine::SetCameraRotationRadians(camera, {pitch, 0.6f, 0.3f});
        const Engine::CameraBasis before = Engine::ResolveCameraBasis(camera);
        Engine::SetCameraRotationRadians(camera, Engine::GetCameraRotationRadians(camera));
        const Engine::CameraBasis after = Engine::ResolveCameraBasis(camera);
        if (DirectX::XMVectorGetX(DirectX::XMVector3Length(DirectX::XMVectorSubtract(
                before.up, after.up))) > 0.0001f || !IsFinite(Engine::CreateCameraViewMatrix(camera)))
        {
            return false;
        }
    }
    return true;
}

bool TestCameraParameterComparison()
{
    Engine::CameraState stored;
    Engine::SetCameraRotationRadians(stored, {0.3f, -0.4f, 0.2f});
    Engine::CameraState current = stored;
    DirectX::XMStoreFloat3(&current.gazePoint, DirectX::XMVectorAdd(
        DirectX::XMLoadFloat3(&current.pos), Engine::ResolveCameraBasis(current).forward));
    if (!Engine::CameraViewParametersMatch(current, stored))
    {
        return false;
    }
    current.pos.x += 0.01f;
    if (Engine::CameraViewParametersMatch(current, stored))
    {
        return false;
    }
    current = stored;
    current.fov += 0.01f;
    if (Engine::CameraViewParametersMatch(current, stored))
    {
        return false;
    }
    current = stored;
    Engine::SetCameraRotationRadians(current, {0.3f, -0.4f, 0.21f});
    if (Engine::CameraViewParametersMatch(current, stored))
    {
        return false;
    }
    return Engine::CameraViewParametersMatch(stored, stored);
}

} // namespace

int main()
{
    if (!TestExactTopDownView() || !TestLegacyWorldUpCompatibility() || !TestDegenerateInputFallback() ||
        !TestLiveCameraEdits() || !TestRotationPoleRoundTrip() || !TestCameraParameterComparison())
    {
        std::cerr << "Camera view tests failed.\n";
        return 1;
    }
    return 0;
}
