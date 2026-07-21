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

    void SceneRenderer::RunFrame(const UiRenderHandler& uiRenderHandler, bool advanceFrame)
    {
        m_engine.RunFrame(uiRenderHandler, advanceFrame);
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

    void SceneRenderer::SetToolUiHandler(ToolUiHandler handler)
    {
        m_toolUiHandler = std::move(handler);
    }

    void SceneRenderer::DrawToolUi()
    {
        if (m_toolUiHandler)
        {
            m_toolUiHandler();
        }
    }

    auto SceneRenderer::GetUiFrameContext() const -> UiFrameContext
    {
        return m_engine.GetUiFrameContext();
    }

    float SceneRenderer::CpuFrameTimeMs() const
    {
        return m_engine.CpuFrameTimeMs();
    }

    void SceneRenderer::SetLightingParams(const RtPbrSurveyEngine::LightingParams& params)
    {
        m_engine.SetLightingParams(params);
    }

    void SceneRenderer::SetShadowSettings(const RtPbrSurveyEngine::ShadowSettings& settings)
    {
        m_engine.SetShadowSettings(settings);
    }

    const RtPbrSurveyEngine::ShadowSettings& SceneRenderer::GetShadowSettings() const
    {
        return m_engine.GetShadowSettings();
    }

    void SceneRenderer::SetTemporalUpscalerSettings(const Engine::TemporalUpscalerSettings& settings)
    {
        m_engine.SetTemporalUpscalerSettings(settings);
    }

    const Engine::TemporalUpscalerSettings& SceneRenderer::GetTemporalUpscalerSettings() const
    {
        return m_engine.GetTemporalUpscalerSettings();
    }

    void SceneRenderer::SetHybridReflectionSettings(const RtPbrSurveyEngine::HybridReflectionSettings& settings)
    {
        m_engine.SetHybridReflectionSettings(settings);
    }

    const RtPbrSurveyEngine::HybridReflectionSettings& SceneRenderer::GetHybridReflectionSettings() const
    {
        return m_engine.GetHybridReflectionSettings();
    }

    void SceneRenderer::SetMaterialParams(UINT materialIndex, const RtPbrSurveyEngine::MaterialParams& params)
    {
        m_engine.SetMaterialParams(materialIndex, params);
    }

    void SceneRenderer::SetRenderingPath(RtPbrSurveyEngine::RenderingPath renderingPath)
    {
        m_engine.SetRenderingPath(renderingPath);
    }

    void SceneRenderer::SetLightingPassDebugGradient(bool enabled)
    {
        m_engine.SetLightingPassDebugGradient(enabled);
    }

    void SceneRenderer::SetBackBufferClearColor(const std::array<float, 4>& color)
    {
        m_engine.SetBackBufferClearColor(color);
    }

    void SceneRenderer::SetDisplayInstanceCount(int count)
    {
        m_engine.SetDisplayInstanceCount(count);
    }

    void SceneRenderer::SetToneMapParams(const RtPbrSurveyEngine::ToneMapParams& params)
    {
        m_engine.SetToneMapParams(params);
    }

    void SceneRenderer::SetRenderViewMode(RtPbrSurveyEngine::RenderViewMode mode)
    {
        m_engine.SetRenderViewMode(mode);
    }

    void SceneRenderer::SetRequestHdrDump(bool request)
    {
        m_engine.SetRequestHdrDump(request);
    }

    void SceneRenderer::ReloadEnvironmentResources(const Engine::ProceduralEnvironmentSettings& settings)
    {
        m_engine.ReloadEnvironmentResources(settings);
    }

    void SceneRenderer::RequestPixelPick(int screenX, int screenY)
    {
        m_engine.RequestPixelPick(screenX, screenY);
    }

    const RtPbrSurveyEngine::PixelPickResult& SceneRenderer::GetPixelPickResult() const
    {
        return m_engine.GetPixelPickResult();
    }

    void SceneRenderer::SetSpecularDebugLineSettings(const RtPbrSurveyEngine::SpecularDebugLineSettings& settings)
    {
        m_engine.SetSpecularDebugLineSettings(settings);
    }

    const RtPbrSurveyEngine::SpecularDebugLineSettings& SceneRenderer::GetSpecularDebugLineSettings() const
    {
        return m_engine.GetSpecularDebugLineSettings();
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
