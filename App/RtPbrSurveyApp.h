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

    enum class CameraMode
    {
        FreeLook,
        Arcball,
    };

    enum class AppMode
    {
        SceneSelect,
        Running,
    };

    static constexpr int kDefaultSceneIndex = 0;

    void CreateSampleScenes();
    void LoadSceneCpuData(int sceneIndex);
    void OpenSelectedScene();
    void CloseRunningScene();
    void InitializeImGui();
    void UpdateUiFrame();
    void FlushD3D12DebugMessages();
    void LogFpsToFile(float cpuFrameTimeMs);
    Engine::SampleScene& LoadedScene();
    const Engine::SampleScene& LoadedScene() const;
    void DrawDebugUi(const RtPbrSurveyEngine::UiFrameContext& context);

    static constexpr UINT kMaxInstanceCount = RtPbrSurveyEngine::kMaxInstanceCount;
    static constexpr float kMousePanSpeed = 0.01f;
    static constexpr float kMouseCameraRotationSpeed = 0.005f;
    static constexpr float kMouseWheelCameraSpeed = 0.25f;
    static constexpr float kMouseWheelFovSpeed = 1.0f;
    static constexpr float kCameraPitchLimit = 1.5f;
    static constexpr float kObjectViewerDollySpeed = 0.5f;
    static constexpr float kObjectViewerPanSpeed = 0.008f;
    static constexpr float kObjectViewerPitchLimit = 1.4f;
    static constexpr int kObjectViewerOrbitPitchDeadZonePixels = 3;
    static constexpr float kCameraVerticalSpeed = 0.01f;
    static constexpr float kCameraFovZoomSpeed = 2.0f;
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

    int m_displayInstanceCount = static_cast<int>(kMaxInstanceCount);
    float m_meshScale = 0.5f;
    bool m_isPlaying = false;

    bool m_isDragging = false;
    bool m_isMiddleDragging = false;
    bool m_isRightDragging = false;
    int m_lastMouseX = 0;
    int m_lastMouseY = 0;
    XMFLOAT3 m_lastArcballVector = {0.0f, 0.0f, 1.0f};
    XMFLOAT4 m_dragRotation = {0.0f, 0.0f, 0.0f, 1.0f};
    XMFLOAT2 m_dragPan = {0.0f, 0.0f};

    CameraMode m_cameraMode = CameraMode::Arcball;
    float m_cameraSpeedMultiplier = 1.0f;
    float m_objectViewerYaw = 0.0f;
    float m_objectViewerPitch = 0.0f;
    float m_objectViewerDistance = 5.0f;
    XMFLOAT3 m_objectViewerPivot = {0.0f, 0.0f, 0.0f};

    void InitObjectViewerFromCamera();
    void SetObjectViewerOrbitFromOffset(DirectX::FXMVECTOR offset);
    void UpdateObjectViewerCamera();
    bool IsGltfViewerSceneIndex(int index) const;

    std::chrono::steady_clock::time_point m_prevTime;

    Platform::WindowInfo m_windowInfo;
    Platform::CommandLineOptions m_commandLineOptions;

    GraphicsDevice m_graphicsDevice;
    ComPtr<ID3D12DescriptorHeap> m_imguiHeap;
    Engine::ImGuiSystem m_imguiSystem;

    RtPbrSurvey::SceneRenderer m_sceneRenderer;
    App::SceneConfigManager m_sceneConfig;

    // Debug logging to file (-LogToFile / -LogFPS).
    ComPtr<ID3D12InfoQueue> m_d3d12InfoQueue;
    FILE* m_logFile = nullptr;
    UINT64 m_fpsLogFrameCounter = 0;
};
