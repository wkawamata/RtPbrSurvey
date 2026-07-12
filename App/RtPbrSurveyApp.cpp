//*********************************************************
//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
//*********************************************************

#include "stdafx.h"

#include <algorithm>
#include <cmath>
#include <fcntl.h>
#include <io.h>
#include <share.h>
#include <sys/stat.h>
#include "RtPbrSurveyApp.h"
#include "../Platform/Win32Application.h"
#include "../Platform/AssetPath.h"
#include "../Scene/SceneFactory.h"
#include "imgui.h"
#include "ImGuiWidgets.h"

void RunStagedAllocatorTests(ID3D12Device* device);

namespace
{

XMFLOAT3 ProjectToArcball(int x, int y, UINT width, UINT height)
{
    const float minDimension = static_cast<float>((std::max)(1u, (std::min)(width, height)));
    const float sx = (2.0f * static_cast<float>(x) - static_cast<float>(width)) / minDimension;
    const float sy = (static_cast<float>(height) - 2.0f * static_cast<float>(y)) / minDimension;
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

XMFLOAT4 ArcballDeltaQuaternion(const XMFLOAT3& from, const XMFLOAT3& to)
{
    const XMVECTOR fromVec = XMVector3Normalize(XMLoadFloat3(&from));
    const XMVECTOR toVec = XMVector3Normalize(XMLoadFloat3(&to));
    const float dot = XMVectorGetX(XMVector3Dot(fromVec, toVec));

    if (dot > 0.9999f)
    {
        return {0.0f, 0.0f, 0.0f, 1.0f};
    }

    XMVECTOR axis = XMVector3Cross(fromVec, toVec);
    if (dot < -0.9999f)
    {
        axis = XMVector3Cross(fromVec, XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f));
        if (XMVectorGetX(XMVector3LengthSq(axis)) < 0.0001f)
        {
            axis = XMVector3Cross(fromVec, XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f));
        }
    }

    const XMVECTOR quaternion = XMQuaternionNormalize(XMVectorSetW(axis, 1.0f + dot));
    XMFLOAT4 result = {};
    XMStoreFloat4(&result, quaternion);
    return result;
}

} // namespace

RtPbrSurveyApp::RtPbrSurveyApp(UINT width, UINT height, std::wstring name)
    : m_windowInfo(Platform::CreateWindowInfo(width, height, name)), m_prevTime(std::chrono::steady_clock::now()), m_engine(m_graphicsDevice)
{
}

_Use_decl_annotations_ void RtPbrSurveyApp::ParseCommandLineArgs(WCHAR* argv[], int argc)
{
    m_commandLineOptions = Platform::ParseCommandLineOptions(argv, argc);
    if (m_commandLineOptions.useWarpDevice)
    {
        m_windowInfo.title = m_windowInfo.title + L" (WARP)";
    }
}

void RtPbrSurveyApp::OnInit()
{
    CreateSampleScenes();

    GraphicsDeviceDesc deviceDesc = {};
    deviceDesc.hwnd = Win32Application::GetHwnd();
    deviceDesc.swapChainWidth = GetWidth();
    deviceDesc.swapChainHeight = GetHeight();
    deviceDesc.bufferCount = RtPbrSurveyEngine::kSwapChainBufferCount;
    deviceDesc.swapChainFormat = RtPbrSurveyEngine::kSwapChainFormat;
    deviceDesc.useWarpDevice = m_commandLineOptions.useWarpDevice;
    m_graphicsDevice.Initialize(deviceDesc);

    // Open debug log file and query ID3D12InfoQueue for D3D12 message capture.
    if (!m_commandLineOptions.logFilePath.empty())
    {
        int fd;
        errno_t err = _wsopen_s(&fd, m_commandLineOptions.logFilePath.c_str(), _O_WRONLY | _O_CREAT | _O_TRUNC | _O_TEXT,
                                _SH_DENYNO, _S_IREAD | _S_IWRITE);
        if (err == 0)
        {
            m_logFile = _fdopen(fd, "wt");
        }
        if (m_logFile)
        {
            m_graphicsDevice.Device()->QueryInterface(IID_PPV_ARGS(&m_d3d12InfoQueue));
            if (m_d3d12InfoQueue)
            {
                m_d3d12InfoQueue->SetMessageCountLimit(100000);
                FlushD3D12DebugMessages();
            }
        }
    }

    InitializeImGui();
    m_engine.SetUpdateHandler([this]() { UpdateSampleState(); });
    m_engine.SetLightingParams(m_lightingParams);
    m_engine.SetRenderingPath(m_renderingPath);
    m_engine.SetLightingPassDebugGradient(m_lightingPassDebugGradient);
    m_engine.SetBackBufferClearColor(m_backBufferClearColor);
    m_engine.SetDisplayInstanceCount(0);
    m_engine.SetToneMapParams(m_toneMapParams);
    m_engine.SetRenderViewMode(m_renderViewMode);

    m_engine.Initialize(GetWidth(), GetHeight());

    // Initialize scene config paths
    {
        // EXE dir: "C:\work\RtPbrSurvey-work\bin\x64\Debug\"
        std::wstring exeDir = Platform::GetApplicationAssetsPath();
        // Project root: navigate up 3 levels from exeDir
        //   bin\x64\Debug\ -> bin\x64\ -> bin\ -> (project root)
        char defaultsPathA[MAX_PATH];
        {
            WCHAR exeCopy[MAX_PATH];
            wcscpy_s(exeCopy, exeDir.c_str());
            // Remove trailing backslash
            size_t len = wcslen(exeCopy);
            if (len > 0 && exeCopy[len - 1] == L'\\') exeCopy[len - 1] = L'\0';
            // Walk up 3 times (bin\x64\Debug\ -> project root)
            for (int i = 0; i < 3; ++i)
            {
                WCHAR* slash = wcsrchr(exeCopy, L'\\');
                if (slash) *slash = L'\0';
            }
            wcscat_s(exeCopy, L"\\Assets\\Config\\scene_config_default.json");
            WideCharToMultiByte(CP_UTF8, 0, exeCopy, -1, defaultsPathA, MAX_PATH, nullptr, nullptr);
        }

        char appDataPath[MAX_PATH];
        DWORD appDataLen = GetEnvironmentVariableA("APPDATA", appDataPath, MAX_PATH);
        std::string userConfigPath;
        if (appDataLen > 0 && appDataLen < MAX_PATH)
        {
            userConfigPath = std::string(appDataPath) + "\\RtPbrSurvey";
            CreateDirectoryA(userConfigPath.c_str(), nullptr);
            userConfigPath += "\\scene_config.json";
        }
        else
        {
            userConfigPath = std::string(defaultsPathA);
            size_t pos = userConfigPath.rfind('\\');
            if (pos != std::string::npos)
                userConfigPath.resize(pos + 1);
            userConfigPath += "scene_config.json";
        }

        m_sceneConfig.SetPaths(defaultsPathA, userConfigPath);
    }

    if (m_commandLineOptions.autoSelectGltfDamagedHelmet)
    {
        m_selectedSceneIndex = kDefaultSceneIndex;
        OpenSelectedScene();
    }
}

void RtPbrSurveyApp::UpdateSampleState()
{
    auto now = std::chrono::steady_clock::now();
    const float deltaTime = std::chrono::duration<float>(now - m_prevTime).count();
    m_prevTime = now;

    if (m_appMode == AppMode::SceneSelect)
    {
        m_engine.SetDisplayInstanceCount(0);
        return;
    }

    static constexpr float kCameraMoveSpeed = 0.01f;
    const float speedMul = m_cameraSpeedMultiplier;
    if (GetForegroundWindow() == Win32Application::GetHwnd())
    {
        auto& camera = LoadedScene().GetScene().camera;

        if (m_isRightDragging)
        {
            XMVECTOR localMove = XMVectorZero();

            if (GetAsyncKeyState('A') & 0x8000)
                localMove = XMVectorAdd(localMove, XMVectorSet(-kCameraMoveSpeed * speedMul, 0.0f, 0.0f, 0.0f));
            if (GetAsyncKeyState('D') & 0x8000)
                localMove = XMVectorAdd(localMove, XMVectorSet(kCameraMoveSpeed * speedMul, 0.0f, 0.0f, 0.0f));
            if (GetAsyncKeyState('W') & 0x8000)
                localMove = XMVectorAdd(localMove, XMVectorSet(0.0f, 0.0f, kCameraMoveSpeed * speedMul, 0.0f));
            if (GetAsyncKeyState('S') & 0x8000)
                localMove = XMVectorAdd(localMove, XMVectorSet(0.0f, 0.0f, -kCameraMoveSpeed * speedMul, 0.0f));
            if (GetAsyncKeyState('E') & 0x8000)
                localMove = XMVectorAdd(localMove, XMVectorSet(0.0f, kCameraVerticalSpeed * speedMul, 0.0f, 0.0f));
            if (GetAsyncKeyState('Q') & 0x8000)
                localMove = XMVectorAdd(localMove, XMVectorSet(0.0f, -kCameraVerticalSpeed * speedMul, 0.0f, 0.0f));
            if (GetAsyncKeyState('Z') & 0x8000)
                camera.fov = std::clamp(camera.fov - kCameraFovZoomSpeed, 20.0f, 150.0f);
            if (GetAsyncKeyState('C') & 0x8000)
                camera.fov = std::clamp(camera.fov + kCameraFovZoomSpeed, 20.0f, 150.0f);

            const float sy = std::sin(camera.rot.y);
            const float cy = std::cos(camera.rot.y);
            const XMVECTOR forward = XMVectorSet(sy, 0.0f, cy, 0.0f);
            const XMVECTOR right = XMVectorSet(cy, 0.0f, -sy, 0.0f);
            const XMVECTOR worldMove = XMVectorAdd(
                XMVectorAdd(
                    XMVectorScale(forward, XMVectorGetZ(localMove)),
                    XMVectorScale(right, XMVectorGetX(localMove))),
                XMVectorSet(0.0f, XMVectorGetY(localMove), 0.0f, 0.0f));
            XMFLOAT3 move = {};
            XMStoreFloat3(&move, worldMove);
            camera.pos.x += move.x;
            camera.pos.y += move.y;
            camera.pos.z += move.z;

            const XMMATRIX camRot = XMMatrixRotationRollPitchYaw(camera.rot.x, camera.rot.y, camera.rot.z);
            const XMVECTOR fwd = XMVector3TransformNormal(XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f), camRot);
            XMStoreFloat3(&camera.gazePoint, XMLoadFloat3(&camera.pos) + fwd);
        }
        else if (m_cameraMode == CameraMode::FreeLook)
        {
            XMVECTOR localMove = XMVectorZero();
            if (GetAsyncKeyState('A') & 0x8000)
                localMove = XMVectorAdd(localMove, XMVectorSet(-kCameraMoveSpeed * speedMul, 0.0f, 0.0f, 0.0f));
            if (GetAsyncKeyState('D') & 0x8000)
                localMove = XMVectorAdd(localMove, XMVectorSet(kCameraMoveSpeed * speedMul, 0.0f, 0.0f, 0.0f));
            if ((GetAsyncKeyState('W') & 0x8000) && (GetAsyncKeyState(VK_SHIFT) & 0x8000))
                localMove = XMVectorAdd(localMove, XMVectorSet(0.0f, kCameraVerticalSpeed * speedMul, 0.0f, 0.0f));
            if ((GetAsyncKeyState('S') & 0x8000) && (GetAsyncKeyState(VK_SHIFT) & 0x8000))
                localMove = XMVectorAdd(localMove, XMVectorSet(0.0f, -kCameraVerticalSpeed * speedMul, 0.0f, 0.0f));
            if ((GetAsyncKeyState('W') & 0x8000) && !(GetAsyncKeyState(VK_SHIFT) & 0x8000))
                localMove = XMVectorAdd(localMove, XMVectorSet(0.0f, 0.0f, kCameraMoveSpeed * speedMul, 0.0f));
            if ((GetAsyncKeyState('S') & 0x8000) && !(GetAsyncKeyState(VK_SHIFT) & 0x8000))
                localMove = XMVectorAdd(localMove, XMVectorSet(0.0f, 0.0f, -kCameraMoveSpeed * speedMul, 0.0f));

            const XMMATRIX cameraRotation = XMMatrixRotationRollPitchYaw(camera.rot.x, camera.rot.y, camera.rot.z);
            const XMVECTOR worldMove = XMVector3TransformNormal(localMove, cameraRotation);
            XMFLOAT3 move = {};
            XMStoreFloat3(&move, worldMove);
            camera.pos.x += move.x;
            camera.pos.y += move.y;
            camera.pos.z += move.z;

            const XMMATRIX camRot = XMMatrixRotationRollPitchYaw(camera.rot.x, camera.rot.y, camera.rot.z);
            const XMVECTOR fwd = XMVector3TransformNormal(XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f), camRot);
            XMStoreFloat3(&camera.gazePoint, XMLoadFloat3(&camera.pos) + fwd);
        }
        else
        {
            UpdateObjectViewerCamera();
        }
    }

    Engine::SampleSceneUpdateContext sceneUpdate = {};
    sceneUpdate.isPlaying = m_isPlaying;
    sceneUpdate.meshScale = m_meshScale;
    sceneUpdate.dragRotation = m_dragRotation;
    LoadedScene().Update(deltaTime, sceneUpdate);

    m_engine.SetScene(LoadedScene().GetScene());
    m_engine.SetDisplayInstanceCount(LoadedScene().DisplayInstanceCount());
}

void RtPbrSurveyApp::OnKeyDown(UINT8 key)
{
    if (m_appMode == AppMode::SceneSelect && key == VK_ESCAPE)
    {
        DestroyWindow(Win32Application::GetHwnd());
        return;
    }

    if (m_appMode == AppMode::Running && key == VK_ESCAPE)
    {
        CloseRunningScene();
        return;
    }

    if (m_appMode == AppMode::Running && key == VK_SPACE)
    {
        m_isPlaying = !m_isPlaying;
    }

    if (m_appMode == AppMode::Running && key == VK_TAB)
    {
        m_cameraMode = (m_cameraMode == CameraMode::Arcball) ? CameraMode::FreeLook : CameraMode::Arcball;
        if (m_cameraMode == CameraMode::Arcball)
        {
            InitObjectViewerFromCamera();
        }
    }
}

void RtPbrSurveyApp::OnKeyUp(UINT8 key) {}

void RtPbrSurveyApp::OnMouseDown(UINT8 button, int x, int y)
{
    if (m_appMode == AppMode::SceneSelect)
    {
        return;
    }

    if (button == VK_LBUTTON)
    {
        if (m_renderingPath == RtPbrSurveyEngine::RenderingPath::Deferred && (GetAsyncKeyState(VK_CONTROL) & 0x8000))
        {
            m_engine.RequestPixelPick(x, y);
            return;
        }
        m_isDragging = true;
        m_lastMouseX = x;
        m_lastMouseY = y;
        m_lastArcballVector = ProjectToArcball(x, y, GetWidth(), GetHeight());
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

void RtPbrSurveyApp::OnMouseUp(UINT8 button, int x, int y)
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
        if (m_cameraMode == CameraMode::Arcball)
        {
            InitObjectViewerFromCamera();
        }
    }
}

void RtPbrSurveyApp::OnMouseMove(int x, int y)
{
    if (m_appMode == AppMode::SceneSelect)
    {
        return;
    }

    auto& camera = LoadedScene().GetScene().camera;

    if (m_isRightDragging)
    {
        const int dx = x - m_lastMouseX;
        const int dy = y - m_lastMouseY;
        m_lastMouseX = x;
        m_lastMouseY = y;

        camera.rot.x = std::clamp(camera.rot.x + static_cast<float>(dy) * kMouseCameraRotationSpeed,
                                  -kCameraPitchLimit, kCameraPitchLimit);
        camera.rot.y += static_cast<float>(dx) * kMouseCameraRotationSpeed;

        const XMMATRIX camRot = XMMatrixRotationRollPitchYaw(camera.rot.x, camera.rot.y, camera.rot.z);
        const XMVECTOR fwd = XMVector3TransformNormal(XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f), camRot);
        XMStoreFloat3(&camera.gazePoint, XMLoadFloat3(&camera.pos) + fwd);
        return;
    }

    if (m_cameraMode == CameraMode::FreeLook)
    {
        if (m_isDragging)
        {
            const int dx = x - m_lastMouseX;
            const int dy = y - m_lastMouseY;
            m_lastMouseX = x;
            m_lastMouseY = y;

            camera.rot.x = std::clamp(camera.rot.x + static_cast<float>(dy) * kMouseCameraRotationSpeed,
                                      -kCameraPitchLimit, kCameraPitchLimit);
            camera.rot.y += static_cast<float>(dx) * kMouseCameraRotationSpeed;

            const XMMATRIX camRot = XMMatrixRotationRollPitchYaw(camera.rot.x, camera.rot.y, camera.rot.z);
            const XMVECTOR fwd = XMVector3TransformNormal(XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f), camRot);
            XMStoreFloat3(&camera.gazePoint, XMLoadFloat3(&camera.pos) + fwd);
        }
        else if (m_isMiddleDragging)
        {
            const int dx = x - m_lastMouseX;
            const int dy = y - m_lastMouseY;
            m_lastMouseX = x;
            m_lastMouseY = y;

            const XMVECTOR localPan = XMVectorSet(static_cast<float>(dx) * kMousePanSpeed * m_cameraSpeedMultiplier,
                                                   -static_cast<float>(dy) * kMousePanSpeed * m_cameraSpeedMultiplier, 0.0f, 0.0f);
            const XMMATRIX cameraRotation = XMMatrixRotationRollPitchYaw(camera.rot.x, camera.rot.y, camera.rot.z);
            const XMVECTOR worldPan = XMVector3TransformNormal(localPan, cameraRotation);
            XMFLOAT3 pan = {};
            XMStoreFloat3(&pan, worldPan);
            camera.pos.x += pan.x;
            camera.pos.y += pan.y;
            camera.pos.z += pan.z;
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
            XMVECTOR offset = XMLoadFloat3(&camera.pos) - pivot;
            if (dx != 0)
            {
                const XMMATRIX yawRotation =
                    XMMatrixRotationY(static_cast<float>(dx) * kMouseCameraRotationSpeed);
                offset = XMVector3TransformNormal(offset, yawRotation);
            }
            if (dy != 0)
            {
                const XMVECTOR lookDir = XMVector3Normalize(-offset);
                const XMVECTOR right = XMVector3Normalize(
                    XMVector3Cross(XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f), lookDir));
                const XMMATRIX pitchRotation =
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
                                                  -static_cast<float>(dy) * kObjectViewerPanSpeed, 0.0f, 0.0f);
            const XMMATRIX cameraRotation = XMMatrixRotationRollPitchYaw(camera.rot.x, camera.rot.y, camera.rot.z);
            const XMVECTOR worldPan = XMVector3TransformNormal(localPan, cameraRotation);
            XMFLOAT3 pan = {};
            XMStoreFloat3(&pan, worldPan);
            m_objectViewerPivot.x += pan.x;
            m_objectViewerPivot.y += pan.y;
            m_objectViewerPivot.z += pan.z;
        }
    }
}

void RtPbrSurveyApp::OnMouseWheel(int wheelDelta)
{
    if (m_appMode == AppMode::SceneSelect)
    {
        return;
    }

    auto& camera = LoadedScene().GetScene().camera;
    const float wheelSteps = static_cast<float>(wheelDelta) / static_cast<float>(WHEEL_DELTA);

    if (GetAsyncKeyState(VK_CONTROL) & 0x8000)
    {
        camera.fov = std::clamp(camera.fov - wheelSteps * kMouseWheelFovSpeed, 20.0f, 150.0f);
        return;
    }

    if (m_cameraMode == CameraMode::FreeLook)
    {
        const XMVECTOR localMove = XMVectorSet(0.0f, 0.0f, wheelSteps * kMouseWheelCameraSpeed * m_cameraSpeedMultiplier, 0.0f);
        const XMMATRIX cameraRotation = XMMatrixRotationRollPitchYaw(camera.rot.x, camera.rot.y, camera.rot.z);
        const XMVECTOR worldMove = XMVector3TransformNormal(localMove, cameraRotation);
        XMFLOAT3 move = {};
        XMStoreFloat3(&move, worldMove);
        camera.pos.x += move.x;
        camera.pos.y += move.y;
        camera.pos.z += move.z;
    }
    else
    {
        m_objectViewerDistance -= wheelSteps * kObjectViewerDollySpeed;
        m_objectViewerDistance = (std::max)(0.1f, m_objectViewerDistance);
    }
}

void RtPbrSurveyApp::OnWindowSizeChanged(UINT width, UINT height)
{
    m_engine.RequestResize(width, height);
    m_imguiSystem.SetDisplaySize(width, height);
}

void RtPbrSurveyApp::OnIdle()
{
    UpdateUiFrame();
    m_engine.RunFrame([this](ID3D12GraphicsCommandList* commandList) { m_imguiSystem.Render(commandList); });

    // Poll D3D12 debug messages and FPS logging.
    if (m_logFile)
    {
        if (m_d3d12InfoQueue)
        {
            FlushD3D12DebugMessages();
        }
        if (m_commandLineOptions.logFpsInterval > 0)
        {
            ++m_fpsLogFrameCounter;
            if (m_fpsLogFrameCounter % m_commandLineOptions.logFpsInterval == 0)
            {
                LogFpsToFile(m_engine.CpuFrameTimeMs());
            }
        }
    }
}

void RtPbrSurveyApp::OnDestroy()
{
    // Save current scene config before shutdown
    if (m_loadedSceneIndex >= 0)
    {
        m_sceneConfig.SaveCurrentScene(
            m_loadedSceneIndex, *this, m_engine, LoadedScene());
    }

    if (m_logFile)
    {
        FlushD3D12DebugMessages();
    }
    m_engine.Shutdown();
    m_imguiSystem.Shutdown();
    m_imguiHeap.Reset();
    if (m_logFile)
    {
        FlushD3D12DebugMessages();
        fclose(m_logFile);
        m_logFile = nullptr;
    }
    m_d3d12InfoQueue.Reset();
}

void RtPbrSurveyApp::FlushD3D12DebugMessages()
{
    if (!m_d3d12InfoQueue || !m_logFile)
    {
        return;
    }

    const UINT64 count = m_d3d12InfoQueue->GetNumStoredMessages();
    if (count == 0)
    {
        return;
    }

    for (UINT64 i = 0; i < count; ++i)
    {
        SIZE_T len = 0;
        m_d3d12InfoQueue->GetMessage(static_cast<UINT>(i), nullptr, &len);
        std::vector<BYTE> buf(len);
        D3D12_MESSAGE* msg = reinterpret_cast<D3D12_MESSAGE*>(buf.data());
        if (SUCCEEDED(m_d3d12InfoQueue->GetMessage(static_cast<UINT>(i), msg, &len)))
        {
            const char* severity = "INFO";
            switch (msg->Severity)
            {
                case D3D12_MESSAGE_SEVERITY_CORRUPTION: severity = "CORRUPTION"; break;
                case D3D12_MESSAGE_SEVERITY_ERROR:      severity = "ERROR";      break;
                case D3D12_MESSAGE_SEVERITY_WARNING:    severity = "WARNING";    break;
                case D3D12_MESSAGE_SEVERITY_INFO:       severity = "INFO";       break;
                case D3D12_MESSAGE_SEVERITY_MESSAGE:    severity = "MESSAGE";    break;
            }
            fprintf(m_logFile, "[%s] %s\n", severity, msg->pDescription);
        }
    }
    m_d3d12InfoQueue->ClearStoredMessages();
    fflush(m_logFile);
}

void RtPbrSurveyApp::LogFpsToFile(float cpuFrameTimeMs)
{
    if (!m_logFile || cpuFrameTimeMs <= 0.0f)
    {
        return;
    }
    const float fps = 1000.0f / cpuFrameTimeMs;
    fprintf(m_logFile, "[FPS] Frame %llu: %.1f FPS (%.2f ms)\n",
            static_cast<unsigned long long>(m_fpsLogFrameCounter), fps, cpuFrameTimeMs);
    fflush(m_logFile);
}

void RtPbrSurveyApp::CreateSampleScenes()
{
    m_sampleScenes.clear();

    static const Engine::GltfAssetDesc gltfAssets[] = {
        {"DamagedHelmet", "Assets\\Models\\DamagedHelmet\\glTF\\DamagedHelmet.gltf", -10.0f, 0.5f},
        {"Avocado", "Assets\\Models\\Avocado\\glTF\\Avocado.gltf", -10.0f, 0.35f},
        {"BoomBox", "Assets\\Models\\BoomBox\\glTF\\BoomBox.gltf", -6.0f, 1.0f},
        {"Lantern", "Assets\\Models\\Lantern\\glTF\\Lantern.gltf", -10.0f, 0.5f},
        {"Sponza", "Assets\\Models\\Sponza\\glTF\\Sponza.gltf", -10.0f, 0.01f},
        {"FlightHelmet", nullptr, -10.0f, 0.5f},
        {"Suzanne", nullptr, -10.0f, 0.5f},
        {"BoxTextured", nullptr, -10.0f, 0.5f},
        {"CesiumMan", nullptr, -10.0f, 0.5f},
    };
    const int gltfAssetCount = ARRAYSIZE(gltfAssets);

    m_gltfViewerCount = gltfAssetCount;
    for (int i = 0; i < m_gltfViewerCount; i++)
    {
        m_sampleScenes.push_back(std::make_unique<Engine::GltfObjectViewerScene>(gltfAssets[i]));
    }

    m_gltfSceneCount = gltfAssetCount;
    for (int i = 0; i < m_gltfSceneCount; i++)
    {
        m_sampleScenes.push_back(
            std::make_unique<Engine::GltfGridBenchmarkScene>(gltfAssets[i], Engine::GltfGridBenchmarkScene::kMaxInstanceCount));
    }

    m_sampleScenes.push_back(std::make_unique<Engine::MetallicRoughnessSphereScene>(
        Engine::MetallicRoughnessSphereScene::kMaxInstanceCount));
    m_sampleScenes.push_back(std::make_unique<Engine::ShadowTestGroundCubesScene>(
        Engine::ShadowTestGroundCubesScene::kMaxInstanceCount));
    m_sampleScenes.push_back(
        std::make_unique<Engine::AnimatedShadowGridScene>(Engine::AnimatedShadowGridScene::kMaxInstanceCount));
    m_sampleScenes.push_back(
        std::make_unique<Engine::ContactShadowTestScene>(Engine::ContactShadowTestScene::kMaxInstanceCount));
    m_sampleScenes.push_back(
        std::make_unique<Engine::OccluderWallTestScene>(Engine::OccluderWallTestScene::kMaxInstanceCount));

    m_sampleScenes.push_back(Engine::SceneFactory::CreateCornellBox());
}

void RtPbrSurveyApp::LoadSceneCpuData(int sceneIndex)
{
    assert(sceneIndex >= 0 && sceneIndex < static_cast<int>(m_sampleScenes.size()));

    m_loadedSceneIndex = sceneIndex;
    m_loadedScene = m_sampleScenes[static_cast<size_t>(m_loadedSceneIndex)].get();
    m_selectedSceneIndex = m_loadedSceneIndex;
    m_loadedScene->Load();
    m_meshScale = m_loadedScene->DefaultMeshScale();
    m_displayInstanceCount = m_loadedScene->DisplayInstanceCount();
    m_selectedMaterialIndex = 0;
    m_lastArcballVector = {0.0f, 0.0f, 1.0f};
    m_dragRotation = {0.0f, 0.0f, 0.0f, 1.0f};
    m_objectViewerPivot = {0.0f, 0.0f, 0.0f};
    m_sceneResourcesLoaded = false;

    m_cameraMode = IsGltfViewerSceneIndex(m_loadedSceneIndex) ? CameraMode::Arcball : CameraMode::FreeLook;
    if (strstr(m_loadedScene->Name(), "Sponza") != nullptr)
    {
        m_cameraMode = CameraMode::FreeLook;
    }
    if (m_cameraMode == CameraMode::Arcball)
    {
        InitObjectViewerFromCamera();
    }
    else
    {
        auto& camera = LoadedScene().GetScene().camera;
        const XMVECTOR camPos = XMLoadFloat3(&camera.pos);
        const XMVECTOR dir = XMVector3Normalize(XMVectorSet(0.0f, 0.0f, 0.0f, 0.0f) - camPos);
        XMFLOAT3 dirF;
        XMStoreFloat3(&dirF, dir);
        camera.rot.x = std::asin(std::clamp(dirF.y, -1.0f, 1.0f));
        camera.rot.y = std::atan2(dirF.x, dirF.z);
        camera.gazePoint = {0.0f, 0.0f, 0.0f};
    }
}

void RtPbrSurveyApp::OpenSelectedScene()
{
    // Save outgoing scene config before switching
    if (m_loadedSceneIndex >= 0 && m_selectedSceneIndex != m_loadedSceneIndex)
    {
        m_sceneConfig.SaveCurrentScene(
            m_loadedSceneIndex, *this, m_engine,
            *m_sampleScenes[static_cast<size_t>(m_loadedSceneIndex)]);
    }

    if (m_selectedSceneIndex != m_loadedSceneIndex)
    {
        LoadSceneCpuData(m_selectedSceneIndex);
    }

    if (!m_sceneResourcesLoaded)
    {
        m_engine.ReloadSceneResources(LoadedScene().GetScene());
        m_sceneResourcesLoaded = true;
    }

    // Apply saved config for the incoming scene
    m_sceneConfig.LoadAndApplyForScene(
        m_selectedSceneIndex, *this, m_engine, LoadedScene());

    m_displayInstanceCount = LoadedScene().DisplayInstanceCount();
    m_engine.SetDisplayInstanceCount(m_displayInstanceCount);
    m_appMode = AppMode::Running;
}

void RtPbrSurveyApp::CloseRunningScene()
{
    // Save current scene config before closing
    if (m_loadedSceneIndex >= 0)
    {
        m_sceneConfig.SaveCurrentScene(
            m_loadedSceneIndex, *this, m_engine, LoadedScene());
    }

    m_appMode = AppMode::SceneSelect;
    m_isPlaying = false;
    m_isDragging = false;
    m_isMiddleDragging = false;
    if (m_loadedSceneIndex >= 0)
    {
        m_selectedSceneIndex = m_loadedSceneIndex;
    }
    m_displayInstanceCount = 0;
    m_sceneResourcesLoaded = false;
    m_engine.SetDisplayInstanceCount(0);
    m_engine.CloseSceneResources();
}

bool RtPbrSurveyApp::IsGltfViewerSceneIndex(int index) const
{
    return index >= 0 && index < m_gltfViewerCount;
}

void RtPbrSurveyApp::InitObjectViewerFromCamera()
{
    auto& camera = LoadedScene().GetScene().camera;
    const XMVECTOR pivot = XMLoadFloat3(&m_objectViewerPivot);
    const XMVECTOR camPos = XMLoadFloat3(&camera.pos);
    SetObjectViewerOrbitFromOffset(camPos - pivot);
}

void RtPbrSurveyApp::SetObjectViewerOrbitFromOffset(DirectX::FXMVECTOR offset)
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

void RtPbrSurveyApp::UpdateObjectViewerCamera()
{
    auto& camera = LoadedScene().GetScene().camera;
    m_objectViewerPitch = std::clamp(m_objectViewerPitch, -kObjectViewerPitchLimit, kObjectViewerPitchLimit);

    const float cp = std::cos(m_objectViewerPitch);
    const float sp = std::sin(m_objectViewerPitch);
    const float cy = std::cos(m_objectViewerYaw);
    const float sy = std::sin(m_objectViewerYaw);
    camera.pos.x = m_objectViewerPivot.x + m_objectViewerDistance * cp * sy;
    camera.pos.y = m_objectViewerPivot.y + m_objectViewerDistance * sp;
    camera.pos.z = m_objectViewerPivot.z + m_objectViewerDistance * cp * cy;

    const XMVECTOR toPivot = XMLoadFloat3(&m_objectViewerPivot) - XMLoadFloat3(&camera.pos);
    const XMVECTOR dir = XMVector3Normalize(toPivot);
    XMFLOAT3 dirF = {};
    XMStoreFloat3(&dirF, dir);
    camera.rot.x = std::asin(std::clamp(dirF.y, -1.0f, 1.0f));
    camera.rot.y = std::atan2(dirF.x, dirF.z);
    camera.rot.z = 0.0f;

    camera.gazePoint = m_objectViewerPivot;
}

void RtPbrSurveyApp::InitializeImGui()
{
    D3D12_DESCRIPTOR_HEAP_DESC imguiHeapDesc = {};
    imguiHeapDesc.NumDescriptors = kImGuiDescriptorCount;
    imguiHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    imguiHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ThrowIfFailed(m_graphicsDevice.Device()->CreateDescriptorHeap(&imguiHeapDesc, IID_PPV_ARGS(&m_imguiHeap)));

    m_imguiSystem.Initialize(Win32Application::GetHwnd(),
                             m_graphicsDevice,
                             m_imguiHeap.Get(),
                             RtPbrSurveyEngine::kSwapChainBufferCount,
                             RtPbrSurveyEngine::kSwapChainFormat);
}

void RtPbrSurveyApp::UpdateUiFrame()
{
    m_imguiSystem.BeginFrame();
    DrawDebugUi(m_engine.GetUiFrameContext());
    m_imguiSystem.EndFrame();
}

Engine::SampleScene& RtPbrSurveyApp::LoadedScene()
{
    assert(m_loadedScene != nullptr);
    return *m_loadedScene;
}

const Engine::SampleScene& RtPbrSurveyApp::LoadedScene() const
{
    assert(m_loadedScene != nullptr);
    return *m_loadedScene;
}

void RtPbrSurveyApp::DrawDebugUi(const RtPbrSurveyEngine::UiFrameContext& context)
{
    App::DrawDebugUi(*this, context);
}
