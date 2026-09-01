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

#pragma once

#include "App/DebugUi.h"
#include "App/SceneConfig.h"
#include "App/SceneSelectUi.h"
#include "Camera/DebugCameraController.h"
#include "../Engine/RtPbrSurveyEngine.h"
#include "Platform/CommandLineOptions.h"
#include "Platform/IApplication.h"
#include "Platform/WindowInfo.h"
#include "Runtime/SceneRenderer.h"
#include "Scene/SampleScene.h"
#include "Ui/ImGuiSystem.h"

#include <d3d12sdklayers.h>

#include <chrono>
#include <memory>

class RtPbrSurveyApp : public Platform::IApplication
{
public:
    RtPbrSurveyApp(UINT width, UINT height, std::wstring name);

    // IApplication overrides.
    void OnInit() override;
    void OnDestroy() override;
    void OnKeyDown(UINT8 key) override;
    void OnKeyUp(UINT8 key) override;
    void OnMouseDown(UINT8 button, int x, int y) override;
    void OnMouseUp(UINT8 button, int x, int y) override;
    void OnMouseMove(int x, int y) override;
    void OnMouseWheel(int wheelDelta) override;
    void OnWindowSizeChanged(UINT width, UINT height) override;
    void OnIdle() override;

    void ParseCommandLineArgs(_In_reads_(argc) WCHAR* argv[], int argc) override;

    UINT GetWidth() const override
    {
        return m_windowInfo.width;
    }
    UINT GetHeight() const override
    {
        return m_windowInfo.height;
    }
    const WCHAR* GetTitle() const override
    {
        return m_windowInfo.title.c_str();
    }

    void UpdateSampleState();

private:
    friend void App::DrawDebugUi(RtPbrSurveyApp& app, const RtPbrSurveyEngine::UiFrameContext& context);
    friend void App::DrawSceneSelectUi(RtPbrSurveyApp& app);
    friend class App::SceneConfigManager;

    enum class AppMode
    {
        SceneSelect,
        Running,
    };

    static constexpr int kDefaultSceneIndex = 0;

    void CreateSampleScenes();
    void LoadSceneCpuData(int sceneIndex);
    void OpenSelectedScene();
    void ApplyDlssSrCommandLineOptions();
    void CloseRunningScene();
    void InitializeImGui();
    void UpdateUiFrame();
    void UpdateAutomatedCaptureCamera();
    bool HasAutomatedCapture() const;
    void FailAutomatedCapture(const std::string& error);
    void UpdateReflectionHdrDiagnostics();
    void WriteReflectionHdrDiagnosticsReport();
    void FlushD3D12DebugMessages();
    void LogFpsToFile(float cpuFrameTimeMs);
    Engine::SampleScene& LoadedScene();
    const Engine::SampleScene& LoadedScene() const;
    void DrawDebugUi(const RtPbrSurveyEngine::UiFrameContext& context);
    RtPbrSurvey::DebugCameraController& DebugCamera() { return m_debugCamera; }
    const RtPbrSurvey::DebugCameraController& DebugCamera() const { return m_debugCamera; }

    static constexpr UINT kMaxInstanceCount = RtPbrSurveyEngine::kMaxInstanceCount;
    static constexpr UINT kImGuiDescriptorCount = 100;

    std::vector<std::unique_ptr<Engine::SampleScene>> m_sampleScenes;
    int m_gltfViewerCount = 0;
    int m_gltfSceneCount = 0;
    Engine::SampleScene* m_loadedScene = nullptr;
    int m_loadedSceneIndex = -1;
    int m_selectedSceneIndex = kDefaultSceneIndex;
    AppMode m_appMode = AppMode::SceneSelect;
    bool m_sceneResourcesLoaded = false;

    RtPbrSurveyEngine::LightingParams m_lightingParams;
    Engine::ProceduralEnvironmentSettings m_environmentSettings;
    bool m_environmentAutoUpdate = Engine::kUseGpuProceduralEnvMap;
    bool m_environmentReloadPending = false;
    RtPbrSurveyEngine::RenderingPath m_renderingPath = RtPbrSurveyEngine::RenderingPath::Deferred;
    bool m_iblEnabled = true;
    bool m_lightingPassDebugGradient = false;
    bool m_debugUiVisible = false;
    int m_selectedMaterialIndex = 0;
    std::array<float, 4> m_backBufferClearColor = {0.0f, 0.2f, 0.4f, 1.0f};
    RtPbrSurveyEngine::ToneMapParams m_toneMapParams;
    RtPbrSurveyEngine::RenderViewMode m_renderViewMode = RtPbrSurveyEngine::RenderViewMode::LightPass;
    bool m_requestHdrDump = false;
    std::string m_screenshotStatus;

    int m_displayInstanceCount = static_cast<int>(kMaxInstanceCount);
    float m_meshScale = 0.5f;
    bool m_isPlaying = false;
    bool m_framePaused = false;
    bool m_forwardStepRequested = false;

    XMFLOAT4 m_dragRotation = {0.0f, 0.0f, 0.0f, 1.0f};

    bool IsGltfViewerSceneIndex(int index) const;

    std::chrono::steady_clock::time_point m_prevTime;

    Platform::WindowInfo m_windowInfo;
    Platform::CommandLineOptions m_commandLineOptions;

    GraphicsDevice m_graphicsDevice;
    ComPtr<ID3D12DescriptorHeap> m_imguiHeap;
    Engine::ImGuiSystem m_imguiSystem;

    RtPbrSurvey::SceneRenderer m_sceneRenderer;
    RtPbrSurvey::DebugCameraController m_debugCamera;
    App::SceneConfigManager m_sceneConfig;

    // Debug logging to file (-LogToFile / -LogFPS).
    ComPtr<ID3D12InfoQueue> m_d3d12InfoQueue;
    FILE* m_logFile = nullptr;
    UINT64 m_fpsLogFrameCounter = 0;
    UINT64 m_automationFrameCounter = 0;
    bool m_automationScreenshotRequested = false;
    float m_automationOrbitStartYaw = 0.0f;
    float m_automationOrbitDistance = 5.0f;
    Platform::ReflectionCapturePlan m_reflectionCapturePlan;
    size_t m_nextReflectionCaptureIndex = 0;
    size_t m_completedReflectionCaptureCount = 0;
    bool m_reflectionCaptureInFlight = false;
    bool m_reflectionCapturePlanFailed = false;
    bool m_reflectionHdrDiagnosticInFlight = false;
    UINT64 m_reflectionHdrDiagnosticCaptureAutomationFrame = 0;
    bool m_reflectionHdrDiagnosticsComplete = false;
    bool m_reflectionConfidenceStableEvidenceApplied = false;
    bool m_reflectionHistoryDiagnosticResetApplied = false;
    std::vector<Engine::ReflectionHdrDiagnosticFrame> m_reflectionHdrDiagnosticFrames;
};
