#include "stdafx.h"

#include "Camera/DebugCameraController.h"
#include "Camera/CaptureOrbitTrack.h"
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

void DragWithButton(RtPbrSurvey::DebugCameraController& controller, UINT8 button, int dx, int dy)
{
    controller.OnMouseDown(button, 400, 300);
    controller.OnMouseMove(400 + dx, 300 + dy);
    controller.OnMouseUp(button, 400 + dx, 300 + dy);
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

bool TestRightPrimaryDragPreservesArcballOrbit()
{
    Engine::CameraState camera;
    RtPbrSurvey::DebugCameraController controller = MakeController(camera);
    controller.SetPrimaryDragButton(VK_RBUTTON);

    DragWithButton(controller, VK_RBUTTON, 40, 0);
    return NearlyEqual(controller.ObjectViewerYaw(), 0.2f) && NearlyEqual(controller.ObjectViewerPitch(), 0.0f);
}

bool TestRightPrimaryDragIgnoresSelectionAndStopsOnRelease()
{
    Engine::CameraState camera;
    RtPbrSurvey::DebugCameraController controller = MakeController(camera);
    controller.SetPrimaryDragButton(VK_RBUTTON);
    const DirectX::XMFLOAT3 initialPosition = camera.pos;
    DragWithButton(controller, VK_LBUTTON, 40, 20);
    if (!NearlyEqual(camera.pos, initialPosition))
    {
        return false;
    }
    DragWithButton(controller, VK_RBUTTON, 40, 0);
    const DirectX::XMFLOAT3 releasedPosition = camera.pos;
    controller.OnMouseMove(500, 350);
    return NearlyEqual(camera.pos, releasedPosition);
}

bool TestChangingPrimaryButtonResetsDrag()
{
    Engine::CameraState camera;
    RtPbrSurvey::DebugCameraController controller = MakeController(camera);
    controller.OnMouseDown(VK_LBUTTON, 400, 300);
    controller.SetPrimaryDragButton(VK_RBUTTON);
    const DirectX::XMFLOAT3 initialPosition = camera.pos;
    controller.OnMouseMove(440, 300);
    if (!NearlyEqual(camera.pos, initialPosition))
    {
        return false;
    }
    controller.SetPrimaryDragButton(VK_LBUTTON);
    Drag(controller, 40, 0);
    return NearlyEqual(controller.ObjectViewerYaw(), 0.2f);
}

bool TestCaptureOrbitTrack()
{
    Engine::CameraState savedCamera;
    savedCamera.pos = {1.0f, 0.0f, -5.0f};
    savedCamera.gazePoint = {0.0f, 0.0f, 0.0f};
    RtPbrSurvey::DebugCameraController savedController;
    savedController.SetCameraState(&savedCamera);
    savedController.InitObjectViewerFromCamera(savedCamera.gazePoint);
    if (!NearlyEqual(savedCamera.pos, {1.0f, 0.0f, -5.0f}))
        return false;
    savedController.UpdateObjectViewerCamera();
    if (!NearlyEqual(savedCamera.pos, {1.0f, 0.0f, -5.0f}))
        return false;
    const RtPbrSurvey::CaptureOrbitPose baseline = {0, 0.3f, 0.4f, 5.0f, {2.0f, -1.0f, 3.0f}};
    const RtPbrSurvey::CaptureOrbitTrack track(baseline, 90, 60);
    const auto first = track.EvaluateFrame(0);
    const auto second = track.EvaluateFrame(120);
    if (!NearlyEqual(first.yaw, baseline.yaw) || second.timeSeconds != 2 ||
        !NearlyEqual(std::sin(second.yaw), std::sin(baseline.yaw + DirectX::XM_PI)) ||
        !NearlyEqual(second.pitch, baseline.pitch) || !NearlyEqual(second.distance, baseline.distance) ||
        !NearlyEqual(second.pivot, baseline.pivot))
        return false;
    const auto reference = track.EvaluateFrame(73);
    for (unsigned int poll = 0; poll < 1000; ++poll)
    {
        track.EvaluateFrame(1800 + poll);
        const auto repeated = track.EvaluateFrame(73);
        if (repeated.timeSeconds != reference.timeSeconds || repeated.yaw != reference.yaw)
            return false;
    }
    Engine::CameraState camera;
    auto controller = MakeController(camera);
    controller.SetObjectViewerState(second.yaw, second.pitch, second.distance, second.pivot);
    controller.UpdateObjectViewerCamera();
    const DirectX::XMFLOAT3 expected = {
        baseline.pivot.x - baseline.distance * std::cos(baseline.pitch) * std::sin(baseline.yaw),
        baseline.pivot.y + baseline.distance * std::sin(baseline.pitch),
        baseline.pivot.z - baseline.distance * std::cos(baseline.pitch) * std::cos(baseline.yaw)};
    if (!NearlyEqual(camera.pos, expected) || !NearlyEqual(camera.gazePoint, baseline.pivot))
        return false;
    const RtPbrSurvey::CaptureOrbitTrack reverse(baseline, -90, 60);
    if (!NearlyEqual(reverse.EvaluateFrame(60).yaw, baseline.yaw - DirectX::XM_PIDIV2))
        return false;
    const RtPbrSurvey::CaptureOrbitTrack stationary(baseline, 0, 60);
    if (stationary.EvaluateFrame(1800).yaw != first.yaw)
        return false;
    try
    {
        RtPbrSurvey::CaptureOrbitTrack invalid(baseline, 90, 0);
        return false;
    }
    catch (const std::invalid_argument&)
    {
    }
    return true;
}

} // namespace

int main()
{
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
    const float distance = std::sqrt(65.0f);
    if (!NearlyEqual(camera.gazePoint, {0.0f, 4.0f - 4.0f / distance, -7.0f + 7.0f / distance}))
    {
        std::cerr << "FreeLook changed the saved camera direction.\n";
        return 1;
    }

    if (!TestRegularHorizontalAndVerticalOrbit() || !TestNearLimitDragDoesNotCrossPole() ||
        !TestArcballSwitchPreservesEightyEightDegreeCamera() || !TestRightPrimaryDragPreservesArcballOrbit() ||
        !TestRightPrimaryDragIgnoresSelectionAndStopsOnRelease() || !TestChangingPrimaryButtonResetsDrag() ||
        !TestCaptureOrbitTrack())
    {
        std::cerr << "Debug camera controller tests failed.\n";
        return 1;
    }
    return 0;
}
