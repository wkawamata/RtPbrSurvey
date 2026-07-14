#pragma once

#include "Scene/Scene.h"

#include <DirectXMath.h>

namespace RtPbrSurvey
{

class DebugCameraController
{
public:
    enum class Mode
    {
        FreeLook,
        Arcball,
    };

    void SetCameraState(Engine::CameraState* camera);
    void SetWindowSize(UINT width, UINT height);

    Mode GetMode() const;
    void SetMode(Mode mode);
    bool IsRightDragging() const;

    float SpeedMultiplier() const;
    void SetSpeedMultiplier(float multiplier);

    DirectX::XMFLOAT3 ObjectViewerPivot() const;
    float ObjectViewerYaw() const;
    float ObjectViewerPitch() const;
    float ObjectViewerDistance() const;
    void SetObjectViewerState(float yaw, float pitch, float distance, const DirectX::XMFLOAT3& pivot);

    void InitObjectViewerFromCamera();
    void UpdateObjectViewerCamera();
    void ResetInputState();

    void OnMouseDown(UINT8 button, int x, int y);
    void OnMouseUp(UINT8 button, int x, int y);
    void OnMouseMove(int x, int y);
    void OnMouseWheel(int wheelDelta, bool fovZoom);
    void UpdateRightDragKeyboard(bool moveLeft,
                                 bool moveRight,
                                 bool moveForward,
                                 bool moveBackward,
                                 bool moveUp,
                                 bool moveDown,
                                 bool zoomIn,
                                 bool zoomOut);
    void UpdateFreeLookKeyboard(float deltaTime,
                                bool moveLeft,
                                bool moveRight,
                                bool moveForward,
                                bool moveBackward,
                                bool moveUp,
                                bool moveDown,
                                bool zoomIn,
                                bool zoomOut);

private:
    static constexpr float kMousePanSpeed = 0.01f;
    static constexpr float kMouseCameraRotationSpeed = 0.005f;
    static constexpr float kMouseWheelCameraSpeed = 0.25f;
    static constexpr float kMouseWheelFovSpeed = 1.0f;
    static constexpr float kCameraPitchLimit = 1.5f;
    static constexpr int kObjectViewerOrbitPitchDeadZonePixels = 3;
    static constexpr float kObjectViewerDollySpeed = 0.5f;
    static constexpr float kObjectViewerPanSpeed = 0.008f;
    static constexpr float kObjectViewerPitchLimit = 1.4f;
    static constexpr float kCameraMoveSpeed = 0.01f;
    static constexpr float kCameraVerticalSpeed = 0.01f;
    static constexpr float kCameraFovZoomSpeed = 2.0f;

    DirectX::XMFLOAT3 ProjectToArcball(int x, int y) const;
    void SetObjectViewerOrbitFromOffset(DirectX::FXMVECTOR offset);
    Engine::CameraState* Camera();

    Engine::CameraState* m_camera = nullptr;
    UINT m_windowWidth = 1;
    UINT m_windowHeight = 1;

    Mode m_mode = Mode::Arcball;
    float m_speedMultiplier = 1.0f;

    bool m_isDragging = false;
    bool m_isMiddleDragging = false;
    bool m_isRightDragging = false;
    int m_lastMouseX = 0;
    int m_lastMouseY = 0;
    DirectX::XMFLOAT3 m_lastArcballVector = {0.0f, 0.0f, 1.0f};

    float m_objectViewerYaw = 0.0f;
    float m_objectViewerPitch = 0.0f;
    float m_objectViewerDistance = 5.0f;
    DirectX::XMFLOAT3 m_objectViewerPivot = {0.0f, 0.0f, 0.0f};
};

} // namespace RtPbrSurvey
