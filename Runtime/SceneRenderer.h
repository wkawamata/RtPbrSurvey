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

namespace RtPbrSurvey
{
    class SceneRenderer
    {
    public:
        using Scene = RtPbrSurveyEngine::Scene;
        using UiFrameContext = RtPbrSurveyEngine::UiFrameContext;
        using UiRenderHandler = RtPbrSurveyEngine::UiRenderHandler;
        using UpdateHandler = RtPbrSurveyEngine::UpdateHandler;

        explicit SceneRenderer(GraphicsDevice& graphicsDevice);

        void Initialize(UINT width, UINT height);
        void Shutdown();
        void RequestResize(UINT width, UINT height);
        void RunFrame(const UiRenderHandler& uiRenderHandler);

        void SetScene(const Scene& scene);
        void ReloadSceneResources(const Scene& scene);
        void CloseSceneResources();

        void SetUpdateHandler(UpdateHandler handler);
        UiFrameContext GetUiFrameContext() const;
        float CpuFrameTimeMs() const;

        void SetLightingParams(const RtPbrSurveyEngine::LightingParams& params);
        void SetShadowSettings(const RtPbrSurveyEngine::ShadowSettings& settings);
        const RtPbrSurveyEngine::ShadowSettings& GetShadowSettings() const;
        void SetHybridReflectionSettings(const RtPbrSurveyEngine::HybridReflectionSettings& settings);
        const RtPbrSurveyEngine::HybridReflectionSettings& GetHybridReflectionSettings() const;
        void SetMaterialParams(UINT materialIndex, const RtPbrSurveyEngine::MaterialParams& params);
        void SetRenderingPath(RtPbrSurveyEngine::RenderingPath renderingPath);
        void SetLightingPassDebugGradient(bool enabled);
        void SetBackBufferClearColor(const std::array<float, 4>& color);
        void SetDisplayInstanceCount(int count);
        void SetToneMapParams(const RtPbrSurveyEngine::ToneMapParams& params);
        void SetRenderViewMode(RtPbrSurveyEngine::RenderViewMode mode);
        void SetRequestHdrDump(bool request);
        void ReloadEnvironmentResources(const Engine::ProceduralEnvironmentSettings& settings);
        void RequestPixelPick(int screenX, int screenY);
        const RtPbrSurveyEngine::PixelPickResult& GetPixelPickResult() const;
        void SetSpecularDebugLineSettings(const RtPbrSurveyEngine::SpecularDebugLineSettings& settings);
        const RtPbrSurveyEngine::SpecularDebugLineSettings& GetSpecularDebugLineSettings() const;

        RtPbrSurveyEngine& EngineForDebugTools();
        const RtPbrSurveyEngine& EngineForDebugTools() const;

    private:
        RtPbrSurveyEngine m_engine;
    };
}
