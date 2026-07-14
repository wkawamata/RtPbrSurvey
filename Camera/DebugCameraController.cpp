#include "stdafx.h"

#include "DebugCameraController.h"

#include <algorithm>
#include <cmath>
#include <DirectXMathMatrix.inl>
#include <DirectXMathVector.inl>

using DirectX::FXMVECTOR;
using DirectX::XMFLOAT3;
using DirectX::XMMatrixRotationAxis;
using DirectX::XMMatrixRotationRollPitchYaw;
using DirectX::XMMatrixRotationY;
using DirectX::XMStoreFloat3;
using DirectX::XMVector3Cross;
using DirectX::XMVector3Length;
using DirectX::XMVector3Normalize;
using DirectX::XMVector3TransformNormal;
using DirectX::XMVectorAdd;
using DirectX::XMVectorGetX;
using DirectX::XMVectorNegate;
using DirectX::XMVectorSet;
using DirectX::XMVectorSubtract;
using DirectX::XMVectorZero;
using DirectX::XMVECTOR;

namespace RtPbrSurvey
{
void DebugCameraController::SetCameraState(Engine::CameraState* camera)
{
    m_camera = camera;
}

void DebugCameraController::SetWindowSize(UINT width, UINT height)
{
    m_windowWidth = (std::max)(1u, width);
    m_windowHeight = (std::max)(1u, height);
}

auto DebugCameraController::GetMode() const -> Mode
{
    return m_mode;
}

void DebugCameraController::SetMode(Mode mode)
{
    if (m_mode == mode)
    {
        return;
    }

    m_mode = mode;
    if (m_mode == Mode::Arcball)
    {
        InitObjectViewerFromCamera();
    }
}

bool DebugCameraController::IsRightDragging() const
{
    return m_isRightDragging;
}

float DebugCameraController::SpeedMultiplier() const
{
    return m_speedMultiplier;
}

void DebugCameraController::SetSpeedMultiplier(float multiplier)
{
    m_speedMultiplier = (std::max)(0.01f, multiplier);
}

XMFLOAT3 DebugCameraController::ObjectViewerPivot() const
{
    return m_objectViewerPivot;
}

float DebugCameraController::ObjectViewerYaw() const
{
    return m_objectViewerYaw;
}

float DebugCameraController::ObjectViewerPitch() const
{
    return m_objectViewerPitch;
}

float DebugCameraController::ObjectViewerDistance() const
{
    return m_objectViewerDistance;
}

void DebugCameraController::SetObjectViewerState(float yaw, float pitch, float distance, const XMFLOAT3& pivot)
{
    m_objectViewerYaw = yaw;
    m_objectViewerPitch = pitch;
    m_objectViewerDistance = (std::max)(0.1f, distance);
    m_objectViewerPivot = pivot;
    UpdateObjectViewerCamera();
}

void DebugCameraController::InitObjectViewerFromCamera()
{
    Engine::CameraState* camera = Camera();
    if (camera == nullptr)
    {
        return;
    }

    const XMVECTOR pivot = XMLoadFloat3(&m_objectViewerPivot);
    const XMVECTOR camPos = XMLoadFloat3(&camera->pos);
    SetObjectViewerOrbitFromOffset(XMVectorSubtract(camPos, pivot));
}

void DebugCameraController::UpdateObjectViewerCamera()
{
    Engine::CameraState* camera = Camera();
    if (camera == nullptr)
    {
        return;
    }

    m_objectViewerPitch = std::clamp(m_objectViewerPitch, -kObjectViewerPitchLimit, kObjectViewerPitchLimit);

    const float cp = std::cos(m_objectViewerPitch);
    const float sp = std::sin(m_objectViewerPitch);
    const float cy = std::cos(m_objectViewerYaw);
    const float sy = std::sin(m_objectViewerYaw);
    camera->pos.x = m_objectViewerPivot.x + m_objectViewerDistance * cp * sy;
    camera->pos.y = m_objectViewerPivot.y + m_objectViewerDistance * sp;
    camera->pos.z = m_objectViewerPivot.z + m_objectViewerDistance * cp * cy;

    const XMVECTOR toPivot = XMVectorSubtract(XMLoadFloat3(&m_objectViewerPivot), XMLoadFloat3(&camera->pos));
    const XMVECTOR dir = XMVector3Normalize(toPivot);
    XMFLOAT3 dirF = {};
    XMStoreFloat3(&dirF, dir);
    camera->rot.x = std::asin(std::clamp(dirF.y, -1.0f, 1.0f));
    camera->rot.y = std::atan2(dirF.x, dirF.z);
    camera->rot.z = 0.0f;

    camera->gazePoint = m_objectViewerPivot;
}

void DebugCameraController::ResetInputState()
{
    m_isDragging = false;
    m_isMiddleDragging = false;
    m_isRightDragging = false;
}

void DebugCameraController::OnMouseDown(UINT8 button, int x, int y)
{
    if (button == VK_LBUTTON)
    {
        m_isDragging = true;
        m_lastMouseX = x;
        m_lastMouseY = y;
        m_lastArcballVector = ProjectToArcball(x, y);
    }
    else if (button == VK_MBUTTON)
    {
        m_isMiddleDragging = true;
        m_lastMouseX = x;
        m_lastMouseY = y;
    }
    else if (button == VK_RBUTTON)
    {
        m_isRightDragging = true;
        m_lastMouseX = x;
        m_lastMouseY = y;
    }
}

void DebugCameraController::OnMouseUp(UINT8 button, int, int)
{
    if (button == VK_LBUTTON)
    {
        m_isDragging = false;
    }
    else if (button == VK_MBUTTON)
    {
        m_isMiddleDragging = false;
    }
    else if (button == VK_RBUTTON)
    {
        m_isRightDragging = false;
        if (m_mode == Mode::Arcball)
        {
            InitObjectViewerFromCamera();
        }
    }
}

void DebugCameraController::OnMouseMove(int x, int y)
{
    Engine::CameraState* camera = Camera();
    if (camera == nullptr)
    {
        return;
    }

    if (m_isRightDragging)
    {
        const int dx = x - m_lastMouseX;
        const int dy = y - m_lastMouseY;
        m_lastMouseX = x;
        m_lastMouseY = y;

        camera->rot.x = std::clamp(camera->rot.x + static_cast<float>(dy) * kMouseCameraRotationSpeed,
                                   -kCameraPitchLimit, kCameraPitchLimit);
        camera->rot.y += static_cast<float>(dx) * kMouseCameraRotationSpeed;

        const auto camRot = XMMatrixRotationRollPitchYaw(camera->rot.x, camera->rot.y, camera->rot.z);
        const XMVECTOR fwd = XMVector3TransformNormal(XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f), camRot);
        XMStoreFloat3(&camera->gazePoint, XMVectorAdd(XMLoadFloat3(&camera->pos), fwd));
        return;
    }

    if (m_mode == Mode::FreeLook)
    {
        if (m_isDragging)
        {
            const int dx = x - m_lastMouseX;
            const int dy = y - m_lastMouseY;
            m_lastMouseX = x;
            m_lastMouseY = y;

            camera->rot.x = std::clamp(camera->rot.x + static_cast<float>(dy) * kMouseCameraRotationSpeed,
                                       -kCameraPitchLimit, kCameraPitchLimit);
            camera->rot.y += static_cast<float>(dx) * kMouseCameraRotationSpeed;

            const auto camRot = XMMatrixRotationRollPitchYaw(camera->rot.x, camera->rot.y, camera->rot.z);
            const XMVECTOR fwd = XMVector3TransformNormal(XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f), camRot);
            XMStoreFloat3(&camera->gazePoint, XMVectorAdd(XMLoadFloat3(&camera->pos), fwd));
        }
        else if (m_isMiddleDragging)
        {
            const int dx = x - m_lastMouseX;
            const int dy = y - m_lastMouseY;
            m_lastMouseX = x;
            m_lastMouseY = y;

            const XMVECTOR localPan = XMVectorSet(static_cast<float>(dx) * kMousePanSpeed * m_speedMultiplier,
                                                  -static_cast<float>(dy) * kMousePanSpeed * m_speedMultiplier,
                                                  0.0f,
                                                  0.0f);
            const auto cameraRotation = XMMatrixRotationRollPitchYaw(camera->rot.x, camera->rot.y, camera->rot.z);
            const XMVECTOR worldPan = XMVector3TransformNormal(localPan, cameraRotation);
            XMFLOAT3 pan = {};
            XMStoreFloat3(&pan, worldPan);
            camera->pos.x += pan.x;
            camera->pos.y += pan.y;
            camera->pos.z += pan.z;
        }
    }
    else
    {
        if (m_isDragging)
        {
            const int dx = x - m_lastMouseX;
            int dy = y - m_lastMouseY;
            m_lastMouseX = x;
            m_lastMouseY = y;
            if (std::abs(dy) <= kObjectViewerOrbitPitchDeadZonePixels)
            {
                dy = 0;
            }

            const XMVECTOR pivot = XMLoadFloat3(&m_objectViewerPivot);
            XMVECTOR offset = XMVectorSubtract(XMLoadFloat3(&camera->pos), pivot);
            if (dx != 0)
            {
                const auto yawRotation = XMMatrixRotationY(static_cast<float>(dx) * kMouseCameraRotationSpeed);
                offset = XMVector3TransformNormal(offset, yawRotation);
            }
            if (dy != 0)
            {
                const XMVECTOR lookDir = XMVector3Normalize(XMVectorNegate(offset));
                const XMVECTOR right =
                    XMVector3Normalize(XMVector3Cross(XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f), lookDir));
                const auto pitchRotation =
                    XMMatrixRotationAxis(right, static_cast<float>(dy) * kMouseCameraRotationSpeed);
                offset = XMVector3TransformNormal(offset, pitchRotation);
            }
            SetObjectViewerOrbitFromOffset(offset);
            UpdateObjectViewerCamera();
        }
        else if (m_isMiddleDragging)
        {
            const int dx = x - m_lastMouseX;
            const int dy = y - m_lastMouseY;
            m_lastMouseX = x;
            m_lastMouseY = y;

            const XMVECTOR localPan = XMVectorSet(static_cast<float>(dx) * kObjectViewerPanSpeed,
                                                  -static_cast<float>(dy) * kObjectViewerPanSpeed,
                                                  0.0f,
                                                  0.0f);
            const auto cameraRotation = XMMatrixRotationRollPitchYaw(camera->rot.x, camera->rot.y, camera->rot.z);
            const XMVECTOR worldPan = XMVector3TransformNormal(localPan, cameraRotation);
            XMFLOAT3 pan = {};
            XMStoreFloat3(&pan, worldPan);
            m_objectViewerPivot.x += pan.x;
            m_objectViewerPivot.y += pan.y;
            m_objectViewerPivot.z += pan.z;
            UpdateObjectViewerCamera();
        }
    }
}

void DebugCameraController::OnMouseWheel(int wheelDelta, bool fovZoom)
{
    Engine::CameraState* camera = Camera();
    if (camera == nullptr)
    {
        return;
    }

    const float wheelSteps = static_cast<float>(wheelDelta) / static_cast<float>(WHEEL_DELTA);

    if (fovZoom)
    {
        camera->fov = std::clamp(camera->fov - wheelSteps * kMouseWheelFovSpeed, 20.0f, 150.0f);
        return;
    }

    if (m_mode == Mode::FreeLook)
    {
        const XMVECTOR localMove =
            XMVectorSet(0.0f, 0.0f, wheelSteps * kMouseWheelCameraSpeed * m_speedMultiplier, 0.0f);
        const auto cameraRotation = XMMatrixRotationRollPitchYaw(camera->rot.x, camera->rot.y, camera->rot.z);
        const XMVECTOR worldMove = XMVector3TransformNormal(localMove, cameraRotation);
        XMFLOAT3 move = {};
        XMStoreFloat3(&move, worldMove);
        camera->pos.x += move.x;
        camera->pos.y += move.y;
        camera->pos.z += move.z;
    }
    else
    {
        m_objectViewerDistance -= wheelSteps * kObjectViewerDollySpeed;
        m_objectViewerDistance = (std::max)(0.1f, m_objectViewerDistance);
        UpdateObjectViewerCamera();
    }
}

void DebugCameraController::UpdateRightDragKeyboard(bool moveLeft,
                                                    bool moveRight,
                                                    bool moveForward,
                                                    bool moveBackward,
                                                    bool moveUp,
                                                    bool moveDown,
                                                    bool zoomIn,
                                                    bool zoomOut)
{
    Engine::CameraState* camera = Camera();
    if (camera == nullptr)
    {
        return;
    }

    if (zoomIn)
    {
        camera->fov = std::clamp(camera->fov - kCameraFovZoomSpeed, 20.0f, 150.0f);
    }
    if (zoomOut)
    {
        camera->fov = std::clamp(camera->fov + kCameraFovZoomSpeed, 20.0f, 150.0f);
    }

    XMVECTOR localMove = XMVectorZero();
    if (moveLeft)
        localMove = XMVectorAdd(localMove, XMVectorSet(-kCameraMoveSpeed * m_speedMultiplier, 0.0f, 0.0f, 0.0f));
    if (moveRight)
        localMove = XMVectorAdd(localMove, XMVectorSet(kCameraMoveSpeed * m_speedMultiplier, 0.0f, 0.0f, 0.0f));
    if (moveForward)
        localMove = XMVectorAdd(localMove, XMVectorSet(0.0f, 0.0f, kCameraMoveSpeed * m_speedMultiplier, 0.0f));
    if (moveBackward)
        localMove = XMVectorAdd(localMove, XMVectorSet(0.0f, 0.0f, -kCameraMoveSpeed * m_speedMultiplier, 0.0f));
    if (moveUp)
        localMove = XMVectorAdd(localMove, XMVectorSet(0.0f, kCameraVerticalSpeed * m_speedMultiplier, 0.0f, 0.0f));
    if (moveDown)
        localMove = XMVectorAdd(localMove, XMVectorSet(0.0f, -kCameraVerticalSpeed * m_speedMultiplier, 0.0f, 0.0f));

    const float sy = std::sin(camera->rot.y);
    const float cy = std::cos(camera->rot.y);
    const XMVECTOR forward = XMVectorSet(sy, 0.0f, cy, 0.0f);
    const XMVECTOR right = XMVectorSet(cy, 0.0f, -sy, 0.0f);
    const XMVECTOR worldMove = XMVectorAdd(
        XMVectorAdd(
            DirectX::XMVectorScale(forward, DirectX::XMVectorGetZ(localMove)),
            DirectX::XMVectorScale(right, DirectX::XMVectorGetX(localMove))),
        XMVectorSet(0.0f, DirectX::XMVectorGetY(localMove), 0.0f, 0.0f));
    XMFLOAT3 move = {};
    XMStoreFloat3(&move, worldMove);
    camera->pos.x += move.x;
    camera->pos.y += move.y;
    camera->pos.z += move.z;

    const auto camRot = XMMatrixRotationRollPitchYaw(camera->rot.x, camera->rot.y, camera->rot.z);
    const XMVECTOR fwd = XMVector3TransformNormal(XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f), camRot);
    XMStoreFloat3(&camera->gazePoint, XMVectorAdd(XMLoadFloat3(&camera->pos), fwd));
}

void DebugCameraController::UpdateFreeLookKeyboard(float,
                                                   bool moveLeft,
                                                   bool moveRight,
                                                   bool moveForward,
                                                   bool moveBackward,
                                                   bool moveUp,
                                                   bool moveDown,
                                                   bool zoomIn,
                                                   bool zoomOut)
{
    Engine::CameraState* camera = Camera();
    if (camera == nullptr)
    {
        return;
    }

    if (zoomIn)
    {
        camera->fov = std::clamp(camera->fov - kCameraFovZoomSpeed, 20.0f, 150.0f);
    }
    if (zoomOut)
    {
        camera->fov = std::clamp(camera->fov + kCameraFovZoomSpeed, 20.0f, 150.0f);
    }

    XMVECTOR localMove = XMVectorZero();
    if (moveLeft)
        localMove = XMVectorAdd(localMove, XMVectorSet(-kCameraMoveSpeed * m_speedMultiplier, 0.0f, 0.0f, 0.0f));
    if (moveRight)
        localMove = XMVectorAdd(localMove, XMVectorSet(kCameraMoveSpeed * m_speedMultiplier, 0.0f, 0.0f, 0.0f));
    if (moveForward)
        localMove = XMVectorAdd(localMove, XMVectorSet(0.0f, 0.0f, kCameraMoveSpeed * m_speedMultiplier, 0.0f));
    if (moveBackward)
        localMove = XMVectorAdd(localMove, XMVectorSet(0.0f, 0.0f, -kCameraMoveSpeed * m_speedMultiplier, 0.0f));
    if (moveUp)
        localMove = XMVectorAdd(localMove, XMVectorSet(0.0f, kCameraVerticalSpeed * m_speedMultiplier, 0.0f, 0.0f));
    if (moveDown)
        localMove = XMVectorAdd(localMove, XMVectorSet(0.0f, -kCameraVerticalSpeed * m_speedMultiplier, 0.0f, 0.0f));

    const auto cameraRotation = XMMatrixRotationRollPitchYaw(camera->rot.x, camera->rot.y, camera->rot.z);
    const XMVECTOR worldMove = XMVector3TransformNormal(localMove, cameraRotation);
    XMFLOAT3 move = {};
    XMStoreFloat3(&move, worldMove);
    camera->pos.x += move.x;
    camera->pos.y += move.y;
    camera->pos.z += move.z;

    const auto camRot = XMMatrixRotationRollPitchYaw(camera->rot.x, camera->rot.y, camera->rot.z);
    const XMVECTOR fwd = XMVector3TransformNormal(XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f), camRot);
    XMStoreFloat3(&camera->gazePoint, XMVectorAdd(XMLoadFloat3(&camera->pos), fwd));
}

XMFLOAT3 DebugCameraController::ProjectToArcball(int x, int y) const
{
    const float minDimension = static_cast<float>((std::max)(1u, (std::min)(m_windowWidth, m_windowHeight)));
    const float sx = (2.0f * static_cast<float>(x) - static_cast<float>(m_windowWidth)) / minDimension;
    const float sy = (static_cast<float>(m_windowHeight) - 2.0f * static_cast<float>(y)) / minDimension;
    const float lengthSquared = sx * sx + sy * sy;

    XMVECTOR projected = {};
    if (lengthSquared <= 1.0f)
    {
        projected = XMVectorSet(sx, sy, std::sqrt(1.0f - lengthSquared), 0.0f);
    }
    else
    {
        projected = XMVector3Normalize(XMVectorSet(sx, sy, 0.0f, 0.0f));
    }

    XMFLOAT3 result = {};
    XMStoreFloat3(&result, projected);
    return result;
}

void DebugCameraController::SetObjectViewerOrbitFromOffset(FXMVECTOR offset)
{
    m_objectViewerDistance = XMVectorGetX(XMVector3Length(offset));
    if (m_objectViewerDistance < 0.001f)
    {
        m_objectViewerDistance = 5.0f;
        m_objectViewerYaw = 0.0f;
        m_objectViewerPitch = 0.0f;
        return;
    }

    const XMVECTOR dir = XMVector3Normalize(offset);
    XMFLOAT3 dirF = {};
    XMStoreFloat3(&dirF, dir);
    m_objectViewerYaw = std::atan2(dirF.x, dirF.z);
    m_objectViewerPitch = std::asin(std::clamp(dirF.y, -1.0f, 1.0f));
}

Engine::CameraState* DebugCameraController::Camera()
{
    return m_camera;
}

} // namespace RtPbrSurvey
