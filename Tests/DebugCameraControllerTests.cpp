#include "stdafx.h"

#include "Camera/DebugCameraController.h"
#include "Scene/CameraView.h"

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

bool TestIdleFreeLookPreservesSceneTarget()
{
    Engine::CameraState camera;
    camera.pos = {0, 4, -7};
    camera.gazePoint = {0, 0, 0};
    camera.rot = {.519146f, 0, 0};
    const Engine::CameraState original = camera;
    RtPbrSurvey::DebugCameraController controller;
    controller.SetCameraState(&camera);
    controller.SetMode(RtPbrSurvey::DebugCameraController::Mode::FreeLook);
    for (int frame = 0; frame < 30; ++frame)
    {
        controller.UpdateFreeLookKeyboard(1.f/60, false, false, false, false, false, false, false, false);
    }
    return camera.gazePoint.x == original.gazePoint.x && camera.gazePoint.y == original.gazePoint.y &&
           camera.gazePoint.z == original.gazePoint.z && NearlyEqual(camera.pos, original.pos) &&
           NearlyEqual(camera.up, original.up) && NearlyEqual(camera.rot, original.rot);
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
    if (!TestIdleFreeLookPreservesSceneTarget())
    {
        std::cerr << "Idle FreeLook must preserve the scene camera target.\n";
        return 1;
    }
    Engine::CameraState rolledCamera;
    Engine::SetCameraRotationRadians(rolledCamera, {0.2f, 0.3f, 0.45f});
    RtPbrSurvey::DebugCameraController rolledController;
    rolledController.SetCameraState(&rolledCamera);
    rolledController.SetMode(RtPbrSurvey::DebugCameraController::Mode::FreeLook);
    rolledController.UpdateFreeLookKeyboard(0.0f, false, false, false, false, false, false, false, false);
    DirectX::XMFLOAT3 rolledAngles = Engine::GetCameraRotationRadians(rolledCamera);
    if (!NearlyEqual(rolledAngles, rolledCamera.rot))
    {
        std::cerr << "Keyboard update did not preserve camera roll.\n";
        return 1;
    }
    rolledController.OnMouseDown(VK_RBUTTON, 0, 0);
    rolledController.OnMouseMove(20, 10);
    rolledController.OnMouseUp(VK_RBUTTON, 20, 10);
    rolledAngles = Engine::GetCameraRotationRadians(rolledCamera);
    if (!NearlyEqual(rolledAngles, rolledCamera.rot) || !NearlyEqual(rolledAngles.z, 0.45f))
    {
        std::cerr << "Mouse rotation did not synchronize camera roll.\n";
        return 1;
    }
    // Positive DirectX pitch looks downward. A no-input foreground update must
    // preserve the saved validation camera's direction toward the floor.
    Engine::CameraState camera;
    camera.pos = {0.0f, 4.0f, -7.0f};
    camera.gazePoint = {0.0f, 0.0f, 0.0f};
    camera.rot = {std::atan2(4.0f, 7.0f), 0.0f, 0.0f};
    RtPbrSurvey::DebugCameraController controller;
    controller.SetCameraState(&camera);
    controller.SetMode(RtPbrSurvey::DebugCameraController::Mode::FreeLook);
    controller.UpdateFreeLookKeyboard(0.0f, false, false, false, false, false, false, false, false);
    if (!NearlyEqual(camera.gazePoint, {0.0f, 0.0f, 0.0f}))
    {
        std::cerr << "FreeLook changed the saved camera direction.\n";
        return 1;
    }

    if (!TestRegularHorizontalAndVerticalOrbit() || !TestNearLimitDragDoesNotCrossPole() ||
        !TestArcballSwitchPreservesEightyEightDegreeCamera())
    {
        std::cerr << "Debug camera controller tests failed.\n";
        return 1;
    }
    return 0;
}
