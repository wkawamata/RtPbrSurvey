#include "Scene/CameraProjection.h"

#include <DirectXMath.h>
#include <cmath>
#include <iostream>

namespace
{

bool NearlyEqual(float lhs, float rhs, float epsilon = 0.0001f)
{
    return std::abs(lhs - rhs) <= epsilon;
}

bool TestPerspectiveProjection()
{
    Engine::CameraState camera;
    camera.projection = Engine::CameraProjection::Perspective;
    camera.fov = 60.0f;
    camera.nearZ = 0.1f;
    camera.farZ = 1000.0f;

    DirectX::XMFLOAT4X4 actual;
    DirectX::XMFLOAT4X4 expected;
    DirectX::XMStoreFloat4x4(&actual, Engine::CreateCameraProjectionMatrix(camera, 16.0f / 9.0f));
    DirectX::XMStoreFloat4x4(
        &expected,
        DirectX::XMMatrixPerspectiveFovLH(DirectX::XMConvertToRadians(60.0f), 16.0f / 9.0f, 0.1f, 1000.0f));
    return NearlyEqual(actual._11, expected._11) && NearlyEqual(actual._22, expected._22) &&
        NearlyEqual(actual._33, expected._33) && NearlyEqual(actual._43, expected._43);
}

bool TestOrthographicProjection()
{
    Engine::CameraState camera;
    camera.projection = Engine::CameraProjection::Orthographic;
    camera.orthographicHeight = 20.0f;
    camera.nearZ = 0.5f;
    camera.farZ = 500.0f;

    DirectX::XMFLOAT4X4 actual;
    DirectX::XMFLOAT4X4 expected;
    DirectX::XMStoreFloat4x4(&actual, Engine::CreateCameraProjectionMatrix(camera, 2.0f));
    DirectX::XMStoreFloat4x4(&expected, DirectX::XMMatrixOrthographicLH(40.0f, 20.0f, 0.5f, 500.0f));
    return NearlyEqual(actual._11, expected._11) && NearlyEqual(actual._22, expected._22) &&
        NearlyEqual(actual._33, expected._33) && NearlyEqual(actual._43, expected._43);
}

} // namespace

int main()
{
    if (!TestPerspectiveProjection() || !TestOrthographicProjection())
    {
        std::cerr << "Camera projection tests failed.\n";
        return 1;
    }
    return 0;
}
