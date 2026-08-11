#include "Scene/CameraProjection.h"

#include <DirectXMath.h>
#include <cmath>
#include <iostream>
#include <limits>

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

bool TestProjectionFramingConversions()
{
    constexpr float fovYDegrees = 60.0f;
    constexpr float focusDistance = 25.0f;
    const float orthographicHeight = Engine::OrthographicHeightFromPerspectiveFovY(fovYDegrees, focusDistance);
    const float matchedFovY = Engine::PerspectiveFovYFromOrthographicHeight(orthographicHeight, focusDistance);
    return NearlyEqual(orthographicHeight, 2.0f * focusDistance * std::tan(DirectX::XMConvertToRadians(30.0f))) &&
        NearlyEqual(matchedFovY, fovYDegrees);
}

bool TestSmallFovLargeDistanceRoundTrip()
{
    constexpr float fovYDegrees = 0.25f;
    constexpr float focusDistance = 100000.0f;
    const float orthographicHeight = Engine::OrthographicHeightFromPerspectiveFovY(fovYDegrees, focusDistance);
    const float matchedFovY = Engine::PerspectiveFovYFromOrthographicHeight(orthographicHeight, focusDistance);
    return std::isfinite(orthographicHeight) && NearlyEqual(matchedFovY, fovYDegrees, 0.00001f);
}

bool TestStrictInvalidInputs()
{
    const float infinity = std::numeric_limits<float>::infinity();
    const float nan = std::numeric_limits<float>::quiet_NaN();
    return std::isnan(Engine::PerspectiveFovYFromOrthographicHeight(0.0f, 1.0f)) &&
        std::isnan(Engine::PerspectiveFovYFromOrthographicHeight(-1.0f, 1.0f)) &&
        std::isnan(Engine::PerspectiveFovYFromOrthographicHeight(1.0f, 0.0f)) &&
        std::isnan(Engine::PerspectiveFovYFromOrthographicHeight(infinity, 1.0f)) &&
        std::isnan(Engine::PerspectiveFovYFromOrthographicHeight(nan, 1.0f)) &&
        std::isnan(Engine::OrthographicHeightFromPerspectiveFovY(0.0f, 1.0f)) &&
        std::isnan(Engine::OrthographicHeightFromPerspectiveFovY(180.0f, 1.0f)) &&
        std::isnan(Engine::OrthographicHeightFromPerspectiveFovY(60.0f, -1.0f)) &&
        std::isnan(Engine::OrthographicHeightFromPerspectiveFovY(infinity, 1.0f)) &&
        std::isnan(Engine::OrthographicHeightFromPerspectiveFovY(nan, 1.0f));
}

bool TestLegacyMatchClampCompatibility()
{
    const float infinity = std::numeric_limits<float>::infinity();
    const float expectedClampedInput = DirectX::XMConvertToDegrees(2.0f * std::atan(0.5f));
    return NearlyEqual(Engine::MatchPerspectiveToOrthographic(0.0f, 0.0f), expectedClampedInput) &&
        NearlyEqual(Engine::MatchPerspectiveToOrthographic(-10.0f, -20.0f), expectedClampedInput) &&
        NearlyEqual(Engine::MatchPerspectiveToOrthographic(0.001f, 1000000.0f), 0.1f) &&
        NearlyEqual(Engine::MatchPerspectiveToOrthographic(1000000.0f, 0.001f), 179.0f) &&
        NearlyEqual(Engine::MatchPerspectiveToOrthographic(infinity, 1.0f), 179.0f) &&
        NearlyEqual(Engine::MatchPerspectiveToOrthographic(1.0f, infinity), 0.1f) &&
        std::isnan(Engine::MatchPerspectiveToOrthographic(std::numeric_limits<float>::quiet_NaN(), 1.0f));
}

} // namespace

int main()
{
    if (!TestPerspectiveProjection() || !TestOrthographicProjection() || !TestProjectionFramingConversions() ||
        !TestSmallFovLargeDistanceRoundTrip() || !TestStrictInvalidInputs() || !TestLegacyMatchClampCompatibility())
    {
        std::cerr << "Camera projection tests failed.\n";
        return 1;
    }
    return 0;
}
