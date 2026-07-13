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

        RtPbrSurveyEngine& EngineForDebugTools();
        const RtPbrSurveyEngine& EngineForDebugTools() const;

    private:
        RtPbrSurveyEngine m_engine;
    };
}
