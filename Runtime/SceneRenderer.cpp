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

#include "Runtime/SceneRenderer.h"

namespace RtPbrSurvey
{
    SceneRenderer::SceneRenderer(GraphicsDevice& graphicsDevice)
        : m_engine(graphicsDevice)
    {
    }

    void SceneRenderer::Initialize(UINT width, UINT height)
    {
        m_engine.Initialize(width, height);
    }

    void SceneRenderer::Shutdown()
    {
        m_engine.Shutdown();
    }

    void SceneRenderer::RequestResize(UINT width, UINT height)
    {
        m_engine.RequestResize(width, height);
    }

    void SceneRenderer::RunFrame(const UiRenderHandler& uiRenderHandler)
    {
        m_engine.RunFrame(uiRenderHandler);
    }

    void SceneRenderer::SetScene(const Scene& scene)
    {
        m_engine.SetScene(scene);
    }

    void SceneRenderer::ReloadSceneResources(const Scene& scene)
    {
        m_engine.ReloadSceneResources(scene);
    }

    void SceneRenderer::CloseSceneResources()
    {
        m_engine.CloseSceneResources();
    }

    void SceneRenderer::SetUpdateHandler(UpdateHandler handler)
    {
        m_engine.SetUpdateHandler(std::move(handler));
    }

    auto SceneRenderer::GetUiFrameContext() const -> UiFrameContext
    {
        return m_engine.GetUiFrameContext();
    }

    float SceneRenderer::CpuFrameTimeMs() const
    {
        return m_engine.CpuFrameTimeMs();
    }

    RtPbrSurveyEngine& SceneRenderer::EngineForDebugTools()
    {
        return m_engine;
    }

    const RtPbrSurveyEngine& SceneRenderer::EngineForDebugTools() const
    {
        return m_engine;
    }
}
