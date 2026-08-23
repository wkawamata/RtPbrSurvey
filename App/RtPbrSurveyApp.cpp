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
#include <fstream>
#include <io.h>
#include <nlohmann/json.hpp>
#include <share.h>
#include <stdexcept>
#include <sys/stat.h>
#include "RtPbrSurveyApp.h"
#include "../Platform/Win32Application.h"
#include "../Platform/AssetPath.h"
#include "../Renderer/StreamlineAdapter.h"
#include "../Renderer/ReflectionHdrDiagnosticStatistics.h"
#include "../Scene/SceneFactory.h"
#include "imgui.h"
#include "ImGuiWidgets.h"

void RunStagedAllocatorTests(ID3D12Device* device);

namespace
{

RtPbrSurveyEngine::RenderViewMode GetReflectionCaptureRenderViewMode(
    Platform::ReflectionCaptureDebugView debugView)
{
    switch (debugView)
    {
        case Platform::ReflectionCaptureDebugView::TemporalValidity:
            return RtPbrSurveyEngine::RenderViewMode::ReflectionTemporalValidity;
        case Platform::ReflectionCaptureDebugView::GBufferPbrParams:
            return RtPbrSurveyEngine::RenderViewMode::GBufferPBRParams;
        case Platform::ReflectionCaptureDebugView::GBufferNormal:
            return RtPbrSurveyEngine::RenderViewMode::GBufferNormal;
        case Platform::ReflectionCaptureDebugView::ReflectionRayMaterial:
            return RtPbrSurveyEngine::RenderViewMode::ReflectionRayMaterial;
        case Platform::ReflectionCaptureDebugView::EvaluatedRadiance:
            return RtPbrSurveyEngine::RenderViewMode::ReflectionEvaluatedRadiance;
        case Platform::ReflectionCaptureDebugView::ResolvedRadiance:
        default:
            return RtPbrSurveyEngine::RenderViewMode::ReflectionResolvedRadiance;
    }
}

} // namespace

RtPbrSurveyApp::RtPbrSurveyApp(UINT width, UINT height, std::wstring name)
    : m_windowInfo(Platform::CreateWindowInfo(width, height, name)), m_prevTime(std::chrono::steady_clock::now()), m_sceneRenderer(m_graphicsDevice)
{
}

_Use_decl_annotations_ void RtPbrSurveyApp::ParseCommandLineArgs(WCHAR* argv[], int argc)
{
    m_commandLineOptions = Platform::ParseCommandLineOptions(argv, argc);
    if (!m_commandLineOptions.reflectionHdrDiagnosticsPath.empty() &&
        (!m_commandLineOptions.capturePath.empty() || !m_commandLineOptions.reflectionCapturePlanPath.empty()))
    {
        throw std::invalid_argument(
            "-ReflectionHdrDiagnostics is mutually exclusive with screenshot capture automation.");
    }
    if (m_commandLineOptions.autoSelectGltfDamagedHelmet &&
        m_commandLineOptions.autoSelectHybridReflectionEstimatorTest)
    {
        throw std::invalid_argument(
            "-AutoSelectGltfDamagedHelmet and -AutoSelectHybridReflectionEstimatorTest are mutually exclusive.");
    }
    if (!m_commandLineOptions.reflectionCapturePlanPath.empty())
    {
        if (!m_commandLineOptions.capturePath.empty())
        {
            throw std::invalid_argument("-CapturePath and -ReflectionCapturePlan are mutually exclusive.");
        }

        std::string error;
        if (!Platform::LoadReflectionCapturePlan(m_commandLineOptions.reflectionCapturePlanPath,
                                                 m_commandLineOptions.reflectionCaptureVariant,
                                                 m_reflectionCapturePlan,
                                                 error))
        {
            throw std::invalid_argument(error);
        }
    }
    if (m_commandLineOptions.useWarpDevice)
    {
        m_windowInfo.title = m_windowInfo.title + L" (WARP)";
    }
}

void RtPbrSurveyApp::OnInit()
{
    const Engine::StreamlineAdapterInitDesc streamlineInitDesc = {L"RtPbrSurvey"};
    Engine::InitializeStreamlineAdapter(streamlineInitDesc);

    CreateSampleScenes();

    GraphicsDeviceDesc deviceDesc = {};
    deviceDesc.hwnd = Win32Application::GetHwnd();
    deviceDesc.swapChainWidth = GetWidth();
    deviceDesc.swapChainHeight = GetHeight();
    deviceDesc.bufferCount = RtPbrSurveyEngine::kSwapChainBufferCount;
    deviceDesc.swapChainFormat = RtPbrSurveyEngine::kSwapChainFormat;
    deviceDesc.useWarpDevice = m_commandLineOptions.useWarpDevice;
    deviceDesc.deviceCreatedHandler = [](ID3D12Device* device) { Engine::SetStreamlineD3DDevice(device); };
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

    if (m_commandLineOptions.autoSelectGltfDamagedHelmet ||
        m_commandLineOptions.autoSelectHybridReflectionEstimatorTest)
    {
        if (m_commandLineOptions.autoSelectHybridReflectionEstimatorTest)
        {
            const auto scene = std::find_if(
                m_sampleScenes.begin(),
                m_sampleScenes.end(),
                [](const std::unique_ptr<Engine::SampleScene>& candidate)
                {
                    return strcmp(candidate->Name(), "Hybrid Reflection Estimator Test") == 0;
                });
            if (scene == m_sampleScenes.end())
            {
                throw std::runtime_error("Hybrid Reflection Estimator Test scene is unavailable.");
            }
            m_selectedSceneIndex = static_cast<int>(std::distance(m_sampleScenes.begin(), scene));
        }
        else
        {
            m_selectedSceneIndex = kDefaultSceneIndex;
        }
        OpenSelectedScene();
        m_debugUiVisible = false;

        if (m_commandLineOptions.captureReflectionResolvedRadiance)
        {
            m_renderingPath = RtPbrSurveyEngine::RenderingPath::Deferred;
            m_renderViewMode =
                GetReflectionCaptureRenderViewMode(m_commandLineOptions.reflectionCaptureDebugView);
            m_sceneRenderer.SetRenderingPath(m_renderingPath);
            m_sceneRenderer.SetRenderViewMode(m_renderViewMode);

            RtPbrSurveyEngine::HybridReflectionSettings reflectionSettings =
                m_sceneRenderer.GetHybridReflectionSettings();
            reflectionSettings.enabled = true;
            if (!m_commandLineOptions.reflectionHdrDiagnosticsPath.empty())
            {
                reflectionSettings.contributionEnabled = true;
            }
            reflectionSettings.stochasticSamplingEnabled = m_commandLineOptions.reflectionStochasticSampling;
            reflectionSettings.rejectedPixelNeighborhoodEnabled =
                m_commandLineOptions.reflectionRejectedPixelNeighborhood;
            reflectionSettings.surfaceVarianceFilterEnabled =
                m_commandLineOptions.reflectionSurfaceVarianceFilter;
            if (m_commandLineOptions.hasReflectionTemporalWeight)
            {
                reflectionSettings.temporalHistoryWeight =
                    std::clamp(m_commandLineOptions.reflectionTemporalWeight, 0.0f, 0.98f);
            }
            if (m_commandLineOptions.hasReflectionTemporalNoiseStrength)
            {
                reflectionSettings.temporalNoiseStrength =
                    std::clamp(m_commandLineOptions.reflectionTemporalNoiseStrength, 0.0f, 1.0f);
            }
            m_sceneRenderer.SetHybridReflectionSettings(reflectionSettings);
            m_automationOrbitStartYaw = m_debugCamera.ObjectViewerYaw();
            m_automationOrbitDistance = (std::max)(
                0.1f,
                m_debugCamera.ObjectViewerDistance() * m_commandLineOptions.reflectionCameraDistanceScale);
        }
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

    UpdateAutomatedCaptureCamera();

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

    if (m_appMode == AppMode::Running && key == 'P')
    {
        m_framePaused = !m_framePaused;
        m_forwardStepRequested = false;
    }

    if (m_appMode == AppMode::Running && m_framePaused && key == 'F')
    {
        m_forwardStepRequested = true;
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
    UpdateReflectionHdrDiagnostics();
    if (m_reflectionHdrDiagnosticsComplete)
    {
        return;
    }

    if (HasAutomatedCapture())
    {
        if (const std::optional<RtPbrSurvey::ScreenshotResult> result = m_sceneRenderer.ConsumeScreenshotResult())
        {
            m_screenshotStatus = result->succeeded ?
                "Saved: " + result->path.string() :
                "Capture failed: " + result->error;

            if (!m_reflectionCapturePlan.captures.empty())
            {
                m_reflectionCaptureInFlight = false;
                if (!result->succeeded)
                {
                    FailAutomatedCapture(result->error);
                }
                else
                {
                    ++m_completedReflectionCaptureCount;
                }
            }

            const bool singleCaptureComplete = m_reflectionCapturePlan.captures.empty();
            const bool capturePlanComplete =
                !m_reflectionCapturePlan.captures.empty() &&
                m_completedReflectionCaptureCount == m_reflectionCapturePlan.captures.size();
            if (m_commandLineOptions.exitAfterCapture && (singleCaptureComplete || capturePlanComplete))
            {
                DestroyWindow(Win32Application::GetHwnd());
                return;
            }
        }
    }

    if (!m_reflectionCapturePlan.captures.empty() && !m_reflectionCapturePlanFailed &&
        m_nextReflectionCaptureIndex < m_reflectionCapturePlan.captures.size())
    {
        const Platform::ReflectionCaptureRequest& capture =
            m_reflectionCapturePlan.captures[m_nextReflectionCaptureIndex];
        if (m_automationFrameCounter > capture.frame)
        {
            FailAutomatedCapture("Missed requested capture frame for case " + capture.caseId + ".");
        }
        else if (m_automationFrameCounter == capture.frame)
        {
            if (m_reflectionCaptureInFlight)
            {
                FailAutomatedCapture("Previous screenshot is still in flight at case " + capture.caseId + ".");
            }
            else
            {
                m_sceneRenderer.RequestScreenshot({capture.path});
                m_reflectionCaptureInFlight = true;
                ++m_nextReflectionCaptureIndex;
            }
        }
    }
    else if (m_reflectionCapturePlan.captures.empty() && !m_commandLineOptions.capturePath.empty() &&
             !m_automationScreenshotRequested &&
             m_automationFrameCounter >= m_commandLineOptions.captureAfterFrames)
    {
        m_sceneRenderer.RequestScreenshot({m_commandLineOptions.capturePath});
        m_automationScreenshotRequested = true;
    }

    if (m_reflectionCapturePlanFailed && m_commandLineOptions.exitAfterCapture)
    {
        return;
    }

    UpdateUiFrame();
    const bool advanceFrame = !m_framePaused || m_forwardStepRequested;
    m_forwardStepRequested = false;
    m_sceneRenderer.RunFrame(
        [this](ID3D12GraphicsCommandList* commandList) { m_imguiSystem.Render(commandList); }, advanceFrame);

    if (HasAutomatedCapture())
    {
        ++m_automationFrameCounter;
    }

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

void RtPbrSurveyApp::UpdateAutomatedCaptureCamera()
{
    if (!m_commandLineOptions.captureReflectionResolvedRadiance ||
        m_debugCamera.GetMode() != RtPbrSurvey::DebugCameraController::Mode::Arcball)
    {
        return;
    }

    if (!m_reflectionCapturePlan.cameraKeyframes.empty())
    {
        const std::vector<Platform::ReflectionCaptureCameraKeyframe>& keyframes =
            m_reflectionCapturePlan.cameraKeyframes;
        const auto upper = std::upper_bound(
            keyframes.begin(), keyframes.end(), m_automationFrameCounter,
            [](UINT64 frame, const Platform::ReflectionCaptureCameraKeyframe& keyframe)
            {
                return frame < keyframe.frame;
            });

        float yawDegrees = keyframes.back().yawDegrees;
        if (upper != keyframes.begin() && upper != keyframes.end())
        {
            const Platform::ReflectionCaptureCameraKeyframe& next = *upper;
            const Platform::ReflectionCaptureCameraKeyframe& previous = *(upper - 1);
            const float progress = static_cast<float>(m_automationFrameCounter - previous.frame) /
                                   static_cast<float>(next.frame - previous.frame);
            yawDegrees = previous.yawDegrees + (next.yawDegrees - previous.yawDegrees) * progress;
        }
        else if (upper == keyframes.begin())
        {
            yawDegrees = keyframes.front().yawDegrees;
        }

        m_debugCamera.SetObjectViewerState(
            m_automationOrbitStartYaw + DirectX::XMConvertToRadians(yawDegrees),
            m_debugCamera.ObjectViewerPitch(),
            m_automationOrbitDistance,
            m_debugCamera.ObjectViewerPivot());
        return;
    }

    if (m_commandLineOptions.capturePath.empty() || m_commandLineOptions.reflectionOrbitFrames == 0)
    {
        return;
    }

    const UINT64 captureFrame = m_commandLineOptions.captureAfterFrames;
    const UINT64 orbitFrameCount =
        (std::min)(static_cast<UINT64>(m_commandLineOptions.reflectionOrbitFrames), captureFrame + 1);
    const UINT64 firstOrbitFrame = captureFrame + 1 - orbitFrameCount;
    if (m_automationFrameCounter < firstOrbitFrame || m_automationFrameCounter > captureFrame)
    {
        return;
    }

    const UINT64 orbitFrame = m_automationFrameCounter - firstOrbitFrame + 1;
    const float progress = static_cast<float>(orbitFrame) / static_cast<float>(orbitFrameCount);
    const float yawOffset = DirectX::XMConvertToRadians(m_commandLineOptions.reflectionOrbitDegrees) * progress;
    m_debugCamera.SetObjectViewerState(m_automationOrbitStartYaw + yawOffset,
                                       m_debugCamera.ObjectViewerPitch(),
                                       m_automationOrbitDistance,
                                       m_debugCamera.ObjectViewerPivot());
}

bool RtPbrSurveyApp::HasAutomatedCapture() const
{
    return !m_commandLineOptions.capturePath.empty() || !m_reflectionCapturePlan.captures.empty() ||
           !m_commandLineOptions.reflectionHdrDiagnosticsPath.empty();
}

void RtPbrSurveyApp::UpdateReflectionHdrDiagnostics()
{
    if (m_commandLineOptions.reflectionHdrDiagnosticsPath.empty())
    {
        return;
    }

    if (std::optional<Engine::ReflectionHdrDiagnosticFrame> frame =
            m_sceneRenderer.ConsumeReflectionHdrDiagnosticFrame())
    {
        m_reflectionHdrDiagnosticFrames.push_back(std::move(*frame));
        m_reflectionHdrDiagnosticInFlight = false;
    }

    if (m_reflectionHdrDiagnosticFrames.size() >= m_commandLineOptions.reflectionHdrDiagnosticsFrames)
    {
        WriteReflectionHdrDiagnosticsReport();
        m_reflectionHdrDiagnosticsComplete = true;
        DestroyWindow(Win32Application::GetHwnd());
        return;
    }

    if (!m_reflectionHdrDiagnosticInFlight &&
        m_automationFrameCounter >= m_commandLineOptions.reflectionHdrDiagnosticsWarmupFrames)
    {
        const Engine::ReflectionHdrDiagnosticRoi roi = {
            m_commandLineOptions.reflectionHdrDiagnosticsRoiX,
            m_commandLineOptions.reflectionHdrDiagnosticsRoiY,
            m_commandLineOptions.reflectionHdrDiagnosticsRoiWidth,
            m_commandLineOptions.reflectionHdrDiagnosticsRoiHeight};
        m_sceneRenderer.RequestReflectionHdrDiagnosticCapture(roi);
        m_reflectionHdrDiagnosticInFlight = true;
    }
}

void RtPbrSurveyApp::WriteReflectionHdrDiagnosticsReport()
{
    using json = nlohmann::json;

    const auto meanLuminance = [](const std::vector<Engine::ReflectionHdrDiagnosticSample>& samples)
    {
        double sum = 0.0;
        for (const Engine::ReflectionHdrDiagnosticSample& sample : samples)
        {
            sum += 0.2126 * sample.r + 0.7152 * sample.g + 0.0722 * sample.b;
        }
        return samples.empty() ? 0.0 : sum / static_cast<double>(samples.size());
    };

    json frameValues = json::array();
    for (size_t frameIndex = 0; frameIndex < m_reflectionHdrDiagnosticFrames.size(); ++frameIndex)
    {
        const Engine::ReflectionHdrDiagnosticFrame& frame = m_reflectionHdrDiagnosticFrames[frameIndex];
        size_t hitCount = 0;
        size_t acceptedCount = 0;
        size_t depthRejectCount = 0;
        size_t normalRejectCount = 0;
        for (size_t sampleIndex = 0; sampleIndex < frame.rayHit.size(); ++sampleIndex)
        {
            hitCount += frame.rayHit[sampleIndex].g >= 0.5f ? 1 : 0;
            const float status = frame.resolvedRadiance[sampleIndex].a;
            acceptedCount += status >= 0.875f ? 1 : 0;
            depthRejectCount += status >= 0.375f && status < 0.625f ? 1 : 0;
            normalRejectCount += status >= 0.625f && status < 0.875f ? 1 : 0;
        }
        const double sampleCount = static_cast<double>(frame.rayHit.size());
        frameValues.push_back({
            {"index", frameIndex},
            {"samplingFrameIndex", frame.samplingFrameIndex},
            {"temporalFrameIndex", frame.temporalFrameIndex},
            {"evaluatedMeanLuminance", meanLuminance(frame.evaluatedRadiance)},
            {"resolvedMeanLuminance", meanLuminance(frame.resolvedRadiance)},
            {"hitRate", sampleCount > 0.0 ? static_cast<double>(hitCount) / sampleCount : 0.0},
            {"temporalAcceptanceRate", sampleCount > 0.0 ? static_cast<double>(acceptedCount) / sampleCount : 0.0},
            {"depthRejectRate", sampleCount > 0.0 ? static_cast<double>(depthRejectCount) / sampleCount : 0.0},
            {"normalRejectRate", sampleCount > 0.0 ? static_cast<double>(normalRejectCount) / sampleCount : 0.0},
        });
    }

    const Engine::ReflectionHdrDiagnosticRoi roi = m_reflectionHdrDiagnosticFrames.front().roi;
    const RtPbrSurveyEngine::UiFrameContext context = m_sceneRenderer.GetUiFrameContext();
    const auto statisticsToJson = [](const Engine::ReflectionHdrDiagnosticStatistics& statistics)
    {
        json value = {
            {"temporalMeanLuminance", statistics.temporalMeanLuminance},
            {"temporalVariance", statistics.temporalVariance},
            {"temporalStandardDeviation", statistics.temporalStandardDeviation},
            {"coefficientOfVariationMeanEpsilon", 1.0e-6},
            {"frameAbsoluteDifferenceMean", statistics.frameAbsoluteDifferenceMean},
            {"frameAbsoluteDifferenceP95", statistics.frameAbsoluteDifferenceP95},
            {"frameAbsoluteDifferenceP99", statistics.frameAbsoluteDifferenceP99},
            {"maximumLuminance", statistics.maximumLuminance},
        };
        value["coefficientOfVariation"] = statistics.coefficientOfVariationValid ?
            json(statistics.coefficientOfVariation) : json(nullptr);
        return value;
    };
    const Engine::ReflectionHdrDiagnosticStatistics evaluatedStatistics =
        Engine::CalculateReflectionHdrDiagnosticStatistics(
            m_reflectionHdrDiagnosticFrames, Engine::ReflectionHdrDiagnosticSignal::EvaluatedRadiance);
    const Engine::ReflectionHdrDiagnosticStatistics resolvedStatistics =
        Engine::CalculateReflectionHdrDiagnosticStatistics(
            m_reflectionHdrDiagnosticFrames, Engine::ReflectionHdrDiagnosticSignal::ResolvedRadiance);
    const Engine::ReflectionHdrDiagnosticBaselineComparison baselineComparison =
        Engine::CompareReflectionHdrDiagnosticsToCurrentEstimatorMeanBaseline(m_reflectionHdrDiagnosticFrames);
    const json report = {
        {"schemaVersion", 1},
        {"signalDomain", "linear-hdr"},
        {"reference", "none"},
        {"scene", LoadedScene().Name()},
        {"warmupFrames", m_commandLineOptions.reflectionHdrDiagnosticsWarmupFrames},
        {"measurementFrames", m_reflectionHdrDiagnosticFrames.size()},
        {"coordinateSpace", "render-pixels"},
        {"renderSize", {{"width", context.renderWidth}, {"height", context.renderHeight}}},
        {"roi", {{"x", roi.x}, {"y", roi.y}, {"width", roi.width}, {"height", roi.height}}},
        {"statistics",
         {{"evaluatedRadiance", statisticsToJson(evaluatedStatistics)},
          {"resolvedRadiance", statisticsToJson(resolvedStatistics)}}},
        {"currentEstimatorMeanBaseline",
         {{"name", "High-SPP Current-Estimator Mean Baseline"},
          {"physicalReference", false},
          {"sampleCount", m_reflectionHdrDiagnosticFrames.size()},
          {"meanLuminance", baselineComparison.baselineMeanLuminance},
          {"evaluatedRmse", baselineComparison.evaluatedRmse},
          {"resolvedRmse", baselineComparison.resolvedRmse},
          {"evaluatedRmseByFrame", baselineComparison.evaluatedRmseByFrame},
          {"resolvedRmseByFrame", baselineComparison.resolvedRmseByFrame}}},
        {"frames", std::move(frameValues)},
    };

    const std::filesystem::path outputPath =
        std::filesystem::absolute(m_commandLineOptions.reflectionHdrDiagnosticsPath);
    if (!outputPath.parent_path().empty())
    {
        std::filesystem::create_directories(outputPath.parent_path());
    }
    std::ofstream output(outputPath);
    if (!output)
    {
        throw std::runtime_error("Failed to open reflection HDR diagnostics report path.");
    }
    output << report.dump(2) << '\n';
}

void RtPbrSurveyApp::FailAutomatedCapture(const std::string& error)
{
    m_reflectionCapturePlanFailed = true;
    m_screenshotStatus = "Capture failed: " + error;
    if (m_logFile)
    {
        fprintf(m_logFile, "[ERROR] %s\n", m_screenshotStatus.c_str());
        fflush(m_logFile);
    }
    if (m_commandLineOptions.exitAfterCapture)
    {
        DestroyWindow(Win32Application::GetHwnd());
    }
}

void RtPbrSurveyApp::OnDestroy()
{
    // Save current scene config before shutdown
    if (m_loadedSceneIndex >= 0 && !HasAutomatedCapture())
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
    m_sampleScenes.push_back(Engine::SceneFactory::CreateHostPrimitiveMeshes());
    m_sampleScenes.push_back(Engine::SceneFactory::CreateHybridReflectionEstimatorTest());
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
    m_framePaused = false;
    m_forwardStepRequested = false;
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
    m_framePaused = false;
    m_forwardStepRequested = false;
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
