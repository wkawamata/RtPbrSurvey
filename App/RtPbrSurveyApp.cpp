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

RtPbrSurveyApp::RtPbrSurveyApp(UINT width, UINT height, std::wstring name)
    : m_windowInfo(Platform::CreateWindowInfo(width, height, name)), m_prevTime(std::chrono::steady_clock::now()), m_sceneRenderer(m_graphicsDevice)
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
    m_sceneRenderer.SetUpdateHandler([this]() { UpdateSampleState(); });
    m_sceneRenderer.SetLightingParams(m_lightingParams);
    m_sceneRenderer.SetRenderingPath(m_renderingPath);
    m_sceneRenderer.SetLightingPassDebugGradient(m_lightingPassDebugGradient);
    m_sceneRenderer.SetBackBufferClearColor(m_backBufferClearColor);
    m_sceneRenderer.SetDisplayInstanceCount(0);
    m_sceneRenderer.SetToneMapParams(m_toneMapParams);
    m_sceneRenderer.SetRenderViewMode(m_renderViewMode);

    m_sceneRenderer.Initialize(GetWidth(), GetHeight());

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
        m_debugUiVisible = false;
    }
}

void RtPbrSurveyApp::UpdateSampleState()
{
    auto now = std::chrono::steady_clock::now();
    const float deltaTime = std::chrono::duration<float>(now - m_prevTime).count();
    m_prevTime = now;

    if (m_appMode == AppMode::SceneSelect)
    {
        m_sceneRenderer.SetDisplayInstanceCount(0);
        return;
    }

    if (GetForegroundWindow() == Win32Application::GetHwnd())
    {
        const bool shiftDown = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
        const bool moveLeft = (GetAsyncKeyState('A') & 0x8000) != 0;
        const bool moveRight = (GetAsyncKeyState('D') & 0x8000) != 0;
        const bool wDown = (GetAsyncKeyState('W') & 0x8000) != 0;
        const bool sDown = (GetAsyncKeyState('S') & 0x8000) != 0;
        const bool rightMoveUp = (GetAsyncKeyState('E') & 0x8000) != 0;
        const bool rightMoveDown = (GetAsyncKeyState('Q') & 0x8000) != 0;
        const bool moveUp = wDown && shiftDown;
        const bool moveDown = sDown && shiftDown;
        const bool moveForward = wDown && !shiftDown;
        const bool moveBackward = sDown && !shiftDown;
        const bool zoomIn = (GetAsyncKeyState('Z') & 0x8000) != 0;
        const bool zoomOut = (GetAsyncKeyState('C') & 0x8000) != 0;

        if (m_debugCamera.IsRightDragging())
        {
            m_debugCamera.UpdateRightDragKeyboard(
                moveLeft, moveRight, wDown, sDown, rightMoveUp, rightMoveDown, zoomIn, zoomOut);
        }
        else if (m_debugCamera.GetMode() == RtPbrSurvey::DebugCameraController::Mode::FreeLook)
        {
            m_debugCamera.UpdateFreeLookKeyboard(
                deltaTime, moveLeft, moveRight, moveForward, moveBackward, moveUp, moveDown, zoomIn, zoomOut);
        }
        else
        {
            m_debugCamera.UpdateObjectViewerCamera();
        }
    }

    Engine::SampleSceneUpdateContext sceneUpdate = {};
    sceneUpdate.isPlaying = m_isPlaying;
    sceneUpdate.meshScale = m_meshScale;
    sceneUpdate.dragRotation = m_dragRotation;
    LoadedScene().Update(deltaTime, sceneUpdate);

    m_sceneRenderer.SetScene(LoadedScene().GetScene());
    m_sceneRenderer.SetDisplayInstanceCount(LoadedScene().DisplayInstanceCount());
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

    if (m_appMode == AppMode::Running && key == VK_F1)
    {
        m_debugUiVisible = !m_debugUiVisible;
    }

    if (m_appMode == AppMode::Running && key == VK_TAB)
    {
        using CameraMode = RtPbrSurvey::DebugCameraController::Mode;
        m_debugCamera.SetMode(m_debugCamera.GetMode() == CameraMode::Arcball ? CameraMode::FreeLook : CameraMode::Arcball);
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
            m_sceneRenderer.RequestPixelPick(x, y);
            return;
        }
    }
    m_debugCamera.OnMouseDown(button, x, y);
}

void RtPbrSurveyApp::OnMouseUp(UINT8 button, int x, int y)
{
    m_debugCamera.OnMouseUp(button, x, y);
}

void RtPbrSurveyApp::OnMouseMove(int x, int y)
{
    if (m_appMode == AppMode::SceneSelect)
    {
        return;
    }

    m_debugCamera.OnMouseMove(x, y);
}

void RtPbrSurveyApp::OnMouseWheel(int wheelDelta)
{
    if (m_appMode == AppMode::SceneSelect)
    {
        return;
    }

    m_debugCamera.OnMouseWheel(wheelDelta, (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0);
}

void RtPbrSurveyApp::OnWindowSizeChanged(UINT width, UINT height)
{
    m_sceneRenderer.RequestResize(width, height);
    m_debugCamera.SetWindowSize(width, height);
    m_imguiSystem.SetDisplaySize(width, height);
}

void RtPbrSurveyApp::OnIdle()
{
    UpdateUiFrame();
    m_sceneRenderer.RunFrame([this](ID3D12GraphicsCommandList* commandList) { m_imguiSystem.Render(commandList); });

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
                LogFpsToFile(m_sceneRenderer.CpuFrameTimeMs());
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
            m_loadedSceneIndex, *this, m_sceneRenderer.EngineForDebugTools(), LoadedScene());
    }

    if (m_logFile)
    {
        FlushD3D12DebugMessages();
    }
    m_sceneRenderer.Shutdown();
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
    m_dragRotation = {0.0f, 0.0f, 0.0f, 1.0f};
    m_sceneResourcesLoaded = false;
    m_debugCamera.SetCameraState(&m_loadedScene->GetScene().camera);
    m_debugCamera.SetWindowSize(GetWidth(), GetHeight());

    using CameraMode = RtPbrSurvey::DebugCameraController::Mode;
    CameraMode cameraMode = IsGltfViewerSceneIndex(m_loadedSceneIndex) ? CameraMode::Arcball : CameraMode::FreeLook;
    if (strstr(m_loadedScene->Name(), "Sponza") != nullptr)
    {
        cameraMode = CameraMode::FreeLook;
    }
    m_debugCamera.SetMode(cameraMode);
    if (cameraMode == CameraMode::Arcball)
    {
        m_debugCamera.InitObjectViewerFromCamera();
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
            m_loadedSceneIndex, *this, m_sceneRenderer.EngineForDebugTools(),
            *m_sampleScenes[static_cast<size_t>(m_loadedSceneIndex)]);
    }

    if (m_selectedSceneIndex != m_loadedSceneIndex)
    {
        LoadSceneCpuData(m_selectedSceneIndex);
    }

    if (!m_sceneResourcesLoaded)
    {
        m_sceneRenderer.ReloadSceneResources(LoadedScene().GetScene());
        m_sceneResourcesLoaded = true;
    }

    // Apply saved config for the incoming scene
    m_sceneConfig.LoadAndApplyForScene(
        m_selectedSceneIndex, *this, m_sceneRenderer.EngineForDebugTools(), LoadedScene());

    m_displayInstanceCount = LoadedScene().DisplayInstanceCount();
    m_sceneRenderer.SetDisplayInstanceCount(m_displayInstanceCount);
    m_appMode = AppMode::Running;
    m_debugUiVisible = true;
}

void RtPbrSurveyApp::CloseRunningScene()
{
    // Save current scene config before closing
    if (m_loadedSceneIndex >= 0)
    {
        m_sceneConfig.SaveCurrentScene(
            m_loadedSceneIndex, *this, m_sceneRenderer.EngineForDebugTools(), LoadedScene());
    }

    m_appMode = AppMode::SceneSelect;
    m_isPlaying = false;
    m_debugCamera.ResetInputState();
    if (m_loadedSceneIndex >= 0)
    {
        m_selectedSceneIndex = m_loadedSceneIndex;
    }
    m_displayInstanceCount = 0;
    m_sceneResourcesLoaded = false;
    m_sceneRenderer.SetDisplayInstanceCount(0);
    m_sceneRenderer.CloseSceneResources();
}

bool RtPbrSurveyApp::IsGltfViewerSceneIndex(int index) const
{
    return index >= 0 && index < m_gltfViewerCount;
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
    if (m_appMode == AppMode::SceneSelect || m_debugUiVisible)
    {
        DrawDebugUi(m_sceneRenderer.GetUiFrameContext());
    }
    m_sceneRenderer.DrawToolUi();
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
