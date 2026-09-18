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
#include "App/EvaluationCaseUi.h"
#include "App/SceneConfig.h"
#include "App/SceneEditorSession.h"
#include "App/SceneSelectUi.h"
#include "App/SceneEditorUi.h"
#include "Camera/DebugCameraController.h"
#include "../Engine/RtPbrSurveyEngine.h"
#include "Platform/CommandLineOptions.h"
#include "Platform/IApplication.h"
#include "Platform/WindowInfo.h"
#include "Runtime/SceneRenderer.h"
#include "Runtime/DebugTextureInspector.h"
#include "Runtime/DebugTextureThumbnailScheduler.h"
#include "Runtime/EvaluationState.h"
#include "Scene/SceneDocumentRuntimeScene.h"
#include "Scene/SampleScene.h"
#include "Ui/ImGuiSystem.h"

#include <d3d12sdklayers.h>

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <vector>

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
    friend void App::DrawEvaluationCasesWindow(RtPbrSurveyApp& app, App::EvaluationCaseScope scope);
    friend void App::DrawSceneSelectUi(RtPbrSurveyApp& app);
    friend void App::DrawSceneEditorStartUi(RtPbrSurveyApp& app);
    friend void App::DrawSceneEditorEditUi(RtPbrSurveyApp& app);
    friend class App::SceneConfigManager;

    enum class AppMode
    {
        TopMenu,
        SceneEditorStart,
        SceneEditorEdit,
        Running,
    };

    enum class SceneEditorPendingAction
    {
        None,
        NewDocument,
        LoadDocument,
        ReturnToTopMenu,
    };

    static constexpr int kDefaultSceneIndex = 0;

    void CreateSampleScenes();
    void LoadSceneCpuData(int sceneIndex);
    void LoadFileSceneCpuData();
    void OpenSelectedScene();
    void OpenFileScene();
    void ApplyFileSceneSettings();
    void CreateNewSceneEditorDocument();
    bool LoadSceneEditorDocument(const std::string& path, std::string* error = nullptr);
    void RequestNewSceneEditorDocument();
    void RequestLoadSceneEditorDocument(const std::string& path);
    void RequestReturnToTopMenu();
    bool SaveSceneEditorDocument(bool saveAs, std::string* error = nullptr);
    void ResolveSceneEditorPendingAction(bool saveChanges, bool discardChanges);
    bool SaveSceneEditorRenderPreset(std::string* error = nullptr);
    bool ReloadSceneEditorRenderPreset(std::string* error = nullptr);
    bool AddSceneEditorGltfNode(const std::string& relativePath, std::string* error = nullptr);
    bool RebuildSceneEditorPreview(std::string* error = nullptr);
    void ApplySceneEditorEnvironmentSettings();
    void UpdateSceneEditorSelectionOverlay();
    void ClearSceneEditorSelectionOverlay();
    void ReturnToTopMenu();
    void ApplyDlssSrCommandLineOptions();
    void ApplyPathTracingCommandLineOptions();
    void CloseRunningScene();
    void InitializeImGui();
    void UpdateUiFrame();
    UINT SyncDebugTextureInspectorToEngine();
    void UpdateAutomatedCaptureCamera();
    bool HasAutomatedCapture() const;
    void FailAutomatedCapture(const std::string& error);
    void UpdateReflectionHdrDiagnostics();
    void WriteReflectionHdrDiagnosticsReport();
    void ApplyRayReconstructionCommandLineOverrides();
    void LogRayReconstructionDiagnostics();
    void AccumulatePathTracingCaptureDiagnostics();
    void LogPathTracingCaptureDiagnostics(const RtPbrSurveyEngine::UiFrameContext& context);
    void FlushD3D12DebugMessages();
    void LogFpsToFile(float cpuFrameTimeMs);
    bool CaptureEvaluationState(RtPbrSurvey::EvaluationState& state, std::string* error = nullptr);
    bool RestoreEvaluationState(const RtPbrSurvey::EvaluationState& state, std::string* error = nullptr);
    Engine::SampleScene& LoadedScene();
    const Engine::SampleScene& LoadedScene() const;
    void DrawDebugUi(const RtPbrSurveyEngine::UiFrameContext& context);
    RtPbrSurvey::DebugCameraController& DebugCamera() { return m_debugCamera; }
    const RtPbrSurvey::DebugCameraController& DebugCamera() const { return m_debugCamera; }

    static constexpr UINT kMaxInstanceCount = RtPbrSurveyEngine::kMaxInstanceCount;
    static constexpr UINT kImGuiDescriptorCount = 100;

    std::vector<std::unique_ptr<Engine::SampleScene>> m_sampleScenes;
    std::unique_ptr<Engine::SceneDocumentRuntimeScene> m_fileScene;
    std::unique_ptr<Engine::SceneDocumentRuntimeScene> m_sceneEditorPreviewScene;
    int m_gltfViewerCount = 0;
    int m_gltfSceneCount = 0;
    Engine::SampleScene* m_loadedScene = nullptr;
    int m_loadedSceneIndex = -1;
    int m_selectedSceneIndex = kDefaultSceneIndex;
    AppMode m_appMode = AppMode::TopMenu;
    bool m_sceneResourcesLoaded = false;

    std::optional<App::SceneEditorSession> m_sceneEditorSession;
    std::string m_sceneEditorDocumentPath;
    std::string m_sceneEditorNewName = "New Test Scene";
    std::string m_sceneEditorLoadPath = "Assets/Scenes/TestSceneEditorSmoke/scene.json";
    std::string m_sceneEditorSavePath = "Assets/Scenes/NewTestScene/scene.json";
    std::string m_sceneEditorStatus;
    bool m_sceneEditorPresetDirty = false;
    float m_sceneEditorTranslationStep = 0.25f;
    SceneEditorPendingAction m_sceneEditorPendingAction = SceneEditorPendingAction::None;
    std::string m_sceneEditorPendingLoadPath;
    std::vector<RtPbrSurvey::DebugLineHandle> m_sceneEditorSelectionLineHandles;

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
    RtPbrSurvey::DebugTextureInspectorManager m_debugTextureInspectors;
    RtPbrSurvey::DebugTextureThumbnailScheduler m_debugTextureThumbnailScheduler;
    std::array<uint64_t, RtPbrSurveyEngine::kMaxDebugTextureOutputCount> m_debugTexturePreviewIds = {};
    uint64_t m_debugTextureThumbnailFrameIndex = 0;

    RtPbrSurvey::SceneRenderer m_sceneRenderer;
    RtPbrSurvey::DebugCameraController m_debugCamera;
    App::SceneConfigManager m_sceneConfig;
    RtPbrSurvey::EvaluationStateStore m_evaluationStates;
    RtPbrSurvey::EvaluationRoi m_evaluationRoi;
    int m_selectedEvaluationStateIndex = -1;
    uint64_t m_evaluationDeleteCandidateId = 0;
    std::string m_evaluationStatus;

    // Debug logging to file (-LogToFile / -LogFPS).
    ComPtr<ID3D12InfoQueue> m_d3d12InfoQueue;
    FILE* m_logFile = nullptr;
    UINT64 m_fpsLogFrameCounter = 0;
    UINT64 m_automationFrameCounter = 0;
    bool m_automationScreenshotRequested = false;
    double m_pathTracingGpuTimeSumMs = 0.0;
    float m_pathTracingGpuTimeMinMs = 0.0f;
    float m_pathTracingGpuTimeMaxMs = 0.0f;
    UINT64 m_pathTracingGpuTimingSampleCount = 0;
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
    bool m_lastLoggedRayReconstructionAvailable = false;
    bool m_lastLoggedRayReconstructionInputReadinessAvailable = false;
    bool m_lastLoggedRayReconstructionInputReady = false;
    bool m_lastLoggedRayReconstructionEvaluateAvailable = false;
    bool m_lastLoggedRayReconstructionEvaluateOutputAvailable = false;
    Engine::RayReconstructionSupportStatus m_lastLoggedRayReconstructionStatus =
        Engine::RayReconstructionSupportStatus::NotIntegrated;
    Engine::RayReconstructionReadinessReason m_lastLoggedRayReconstructionReadinessReason =
        Engine::RayReconstructionReadinessReason::NativeEvaluationDisabled;
    Engine::RayReconstructionSupportStatus m_lastLoggedRayReconstructionEvaluateStatus =
        Engine::RayReconstructionSupportStatus::NotIntegrated;
    const char* m_lastLoggedRayReconstructionSupportQueryResultName = "Unavailable";
    const char* m_lastLoggedRayReconstructionEvaluateResultName = "Unavailable";
    std::vector<Engine::ReflectionHdrDiagnosticFrame> m_reflectionHdrDiagnosticFrames;
};
