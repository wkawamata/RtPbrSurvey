#include "stdafx.h"

#include "Camera/DebugCameraController.h"

#include <cmath>
#include <iostream>

namespace
{

constexpr float kEpsilon = 0.0001f;

bool NearlyEqual(float lhs, float rhs, float epsilon = kEpsilon)
{
    return std::abs(lhs - rhs) <= epsilon;
}

bool NearlyEqual(const DirectX::XMFLOAT3& lhs, const DirectX::XMFLOAT3& rhs, float epsilon = kEpsilon)
{
    return NearlyEqual(lhs.x, rhs.x, epsilon) && NearlyEqual(lhs.y, rhs.y, epsilon) &&
        NearlyEqual(lhs.z, rhs.z, epsilon);
}

RtPbrSurvey::DebugCameraController MakeController(Engine::CameraState& camera)
{
    RtPbrSurvey::DebugCameraController controller;
    controller.SetCameraState(&camera);
    controller.SetWindowSize(800, 600);
    controller.SetObjectViewerState(0.0f, 0.0f, 5.0f, {0.0f, 0.0f, 0.0f});
    return controller;
}

void Drag(RtPbrSurvey::DebugCameraController& controller, int dx, int dy)
{
    controller.OnMouseDown(VK_LBUTTON, 400, 300);
    controller.OnMouseMove(400 + dx, 300 + dy);
    controller.OnMouseUp(VK_LBUTTON, 400 + dx, 300 + dy);
}

bool TestRegularHorizontalAndVerticalOrbit()
{
    Engine::CameraState camera;
    RtPbrSurvey::DebugCameraController controller = MakeController(camera);

    Drag(controller, 40, 0);
    if (!NearlyEqual(controller.ObjectViewerYaw(), 0.2f) || !NearlyEqual(controller.ObjectViewerPitch(), 0.0f) ||
        !NearlyEqual(camera.pos.y, 0.0f))
    {
        return false;
    }

    Drag(controller, 0, 40);
    return NearlyEqual(controller.ObjectViewerYaw(), 0.2f) && NearlyEqual(controller.ObjectViewerPitch(), 0.2f);
}

bool TestNearLimitDragDoesNotCrossPole()
{
    Engine::CameraState camera;
    RtPbrSurvey::DebugCameraController controller = MakeController(camera);
    const float initialYaw = 0.4f;
    controller.SetObjectViewerState(initialYaw, DirectX::XMConvertToRadians(87.0f), 5.0f, {0.0f, 0.0f, 0.0f});

    Drag(controller, 0, 1000);
    if (!NearlyEqual(controller.ObjectViewerPitch(), RtPbrSurvey::DebugCameraController::kObjectViewerPitchLimit) ||
        !NearlyEqual(controller.ObjectViewerYaw(), initialYaw))
    {
        return false;
    }

    Drag(controller, 20, 0);
    return NearlyEqual(controller.ObjectViewerPitch(), RtPbrSurvey::DebugCameraController::kObjectViewerPitchLimit) &&
        NearlyEqual(controller.ObjectViewerYaw(), initialYaw + 0.1f);
}

bool TestArcballSwitchPreservesEightyEightDegreeCamera()
{
    Engine::CameraState camera;
    RtPbrSurvey::DebugCameraController source = MakeController(camera);
    source.SetObjectViewerState(0.65f,
                                RtPbrSurvey::DebugCameraController::kObjectViewerPitchLimit,
                                7.0f,
                                {0.0f, 0.0f, 0.0f});
    const DirectX::XMFLOAT3 positionBeforeSwitch = camera.pos;

    RtPbrSurvey::DebugCameraController target;
    target.SetCameraState(&camera);
    target.SetMode(RtPbrSurvey::DebugCameraController::Mode::FreeLook);
    target.SetMode(RtPbrSurvey::DebugCameraController::Mode::Arcball);
    target.UpdateObjectViewerCamera();

    return NearlyEqual(target.ObjectViewerPitch(), RtPbrSurvey::DebugCameraController::kObjectViewerPitchLimit) &&
        NearlyEqual(camera.pos, positionBeforeSwitch, 0.0005f);
}

} // namespace

int main()
{
    if (!TestRegularHorizontalAndVerticalOrbit() || !TestNearLimitDragDoesNotCrossPole() ||
        !TestArcballSwitchPreservesEightyEightDegreeCamera())
    {
        std::cerr << "Debug camera controller tests failed.\n";
        return 1;
    }
    return 0;
}
