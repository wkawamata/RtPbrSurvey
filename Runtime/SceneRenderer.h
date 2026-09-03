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

#include "Engine/RtPbrSurveyEngine.h"
#include "Runtime/DebugLine.h"
#include "Runtime/SceneRendererSettings.h"

#include <functional>

namespace RtPbrSurvey
{
    class SceneRenderer
    {
    public:
        using Scene = RtPbrSurveyEngine::Scene;
        using CameraState = RtPbrSurveyEngine::CameraState;
        using UiFrameContext = RtPbrSurveyEngine::UiFrameContext;
        using UiRenderHandler = RtPbrSurveyEngine::UiRenderHandler;
        using ToolUiHandler = std::function<void()>;
        using UpdateHandler = RtPbrSurveyEngine::UpdateHandler;

        explicit SceneRenderer(GraphicsDevice& graphicsDevice);

        void Initialize(UINT width, UINT height);
        void Shutdown();
        void RequestResize(UINT width, UINT height);
        void RunFrame(const UiRenderHandler& uiRenderHandler, bool advanceFrame = true);

        void SetScene(const Scene& scene);
        void SetCamera(const CameraState& camera);
        const CameraState& GetCamera() const;
        // Rebuilds GPU resources for the scene and keeps a visible instance count clamped to the scene.
        void ReloadSceneResources(const Scene& scene);
        void CloseSceneResources();

        void SetUpdateHandler(UpdateHandler handler);
        void SetToolUiHandler(ToolUiHandler handler);
        void DrawToolUi();
        UiFrameContext GetUiFrameContext() const;
        float CpuFrameTimeMs() const;
        Engine::RenderGraphDocument CaptureRenderGraphDocument() const;
        const std::vector<Engine::RenderGraphBarrierEvent>& GetRenderGraphBarrierEvents() const;
        std::vector<Engine::RenderGraphBarrierDiagnostic> GetRenderGraphBarrierDiagnostics() const;
        bool HasRenderGraphBarrierEvents() const;
        SceneRendererSettings CaptureSettings() const;
        void ApplySettings(const SceneRendererSettings& settings);

        void SetLightingParams(const RtPbrSurveyEngine::LightingParams& params);
        const RtPbrSurveyEngine::LightingParams& GetLightingParams() const;
        void SetShadowSettings(const RtPbrSurveyEngine::ShadowSettings& settings);
        const RtPbrSurveyEngine::ShadowSettings& GetShadowSettings() const;
        void SetTemporalUpscalerSettings(const Engine::TemporalUpscalerSettings& settings);
        const Engine::TemporalUpscalerSettings& GetTemporalUpscalerSettings() const;
        void SetRayReconstructionSettings(const Engine::RayReconstructionSettings& settings);
        const Engine::RayReconstructionSettings& GetRayReconstructionSettings() const;
        void SetHybridReflectionSettings(const RtPbrSurveyEngine::HybridReflectionSettings& settings);
        const RtPbrSurveyEngine::HybridReflectionSettings& GetHybridReflectionSettings() const;
        void ResetHybridReflectionHistoryForDiagnostics();
        void SetMaterialParams(UINT materialIndex, const RtPbrSurveyEngine::MaterialParams& params);
        void SetRenderingPath(RtPbrSurveyEngine::RenderingPath renderingPath);
        RtPbrSurveyEngine::RenderingPath GetRenderingPath() const;
        void SetLightingPassDebugGradient(bool enabled);
        bool GetLightingPassDebugGradient() const;
        void SetBackBufferClearColor(const std::array<float, 4>& color);
        const std::array<float, 4>& GetBackBufferClearColor() const;
        void SetDisplayInstanceCount(int count);
        void SetToneMapParams(const RtPbrSurveyEngine::ToneMapParams& params);
        RtPbrSurveyEngine::ToneMapParams GetToneMapParams() const;
        void SetRenderViewMode(RtPbrSurveyEngine::RenderViewMode mode);
        RtPbrSurveyEngine::RenderViewMode GetRenderViewMode() const;
        void SetDebugTexturePreviewEnabled(bool enabled);
        bool IsDebugTexturePreviewEnabled() const;
        ID3D12Resource* GetDebugTexturePreviewResource() const;
        void SetRequestHdrDump(bool request);
        void RequestReflectionHdrDiagnosticCapture(const Engine::ReflectionHdrDiagnosticRoi& roi);
        std::optional<Engine::ReflectionHdrDiagnosticFrame> ConsumeReflectionHdrDiagnosticFrame();
        void RequestScreenshot(ScreenshotRequest request);
        std::optional<ScreenshotResult> ConsumeScreenshotResult();
        void ReloadEnvironmentResources(const Engine::ProceduralEnvironmentSettings& settings);
        void RequestPixelPick(int screenX, int screenY);
        const RtPbrSurveyEngine::PixelPickResult& GetPixelPickResult() const;
        void SetSpecularDebugLineSettings(const RtPbrSurveyEngine::SpecularDebugLineSettings& settings);
        const RtPbrSurveyEngine::SpecularDebugLineSettings& GetSpecularDebugLineSettings() const;
        DebugLineHandle AddDebugLine(const DebugLineDesc& desc);
        bool UpdateDebugLine(DebugLineHandle handle, const DebugLineDesc& desc);
        void RemoveDebugLine(DebugLineHandle handle);
        void ClearDebugLines();

        RtPbrSurveyEngine& EngineForDebugTools();
        const RtPbrSurveyEngine& EngineForDebugTools() const;

    private:
        RtPbrSurveyEngine m_engine;
        ToolUiHandler m_toolUiHandler;
    };
} // namespace RtPbrSurvey
