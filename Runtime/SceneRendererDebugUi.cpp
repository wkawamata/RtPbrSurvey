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

#include "Runtime/SceneRendererDebugUi.h"

#include "ImGuiWidgets.h"

#include <imgui.h>

namespace
{
    struct ShadowPreset
    {
        const char* label;
        float normalBias;
        float rayTMin;
        bool softShadowEnabled;
        int sampleCount;
        float lightAngularRadius;
        float jitterStrength;
    };

    struct RenderViewItem
    {
        const char* label;
        RtPbrSurveyEngine::RenderViewMode mode;
        bool requiresRayTracing;
        bool requiresReflection;
    };

    const RenderViewItem kRenderViewItems[] = {
        {"Lit",            RtPbrSurveyEngine::RenderViewMode::LightPass,                       false, false},
        {"Albedo",         RtPbrSurveyEngine::RenderViewMode::GBufferAlbedo,                   false, false},
        {"Normal",         RtPbrSurveyEngine::RenderViewMode::GBufferNormal,                   false, false},
        {"Material",       RtPbrSurveyEngine::RenderViewMode::GBufferMaterial,                 false, false},
        {"Motion Vector",  RtPbrSurveyEngine::RenderViewMode::GBufferMotionVector,             false, false},
        {"PBR Params",     RtPbrSurveyEngine::RenderViewMode::GBufferPBRParams,                false, false},
        {"Emissive",       RtPbrSurveyEngine::RenderViewMode::GBufferEmissive,                 false, false},
        {"Depth",          RtPbrSurveyEngine::RenderViewMode::Depth,                           false, false},
        {"Reflection Dir", RtPbrSurveyEngine::RenderViewMode::ReflectionDirection,             false, false},
        {"View Dir",       RtPbrSurveyEngine::RenderViewMode::ViewDirection,                   false, false},
        {"World Pos",      RtPbrSurveyEngine::RenderViewMode::WorldPosition,                   false, false},
        {"NdotV",          RtPbrSurveyEngine::RenderViewMode::NdotV,                           false, false},
        {"IBL Env",        RtPbrSurveyEngine::RenderViewMode::IblEnvironment,                  false, false},
        {"IBL Irradiance", RtPbrSurveyEngine::RenderViewMode::IblDiffuseIrradiance,            false, false},
        {"IBL Prefilter",  RtPbrSurveyEngine::RenderViewMode::IblSpecularPrefilter,            false, false},
        {"IBL BRDF LUT",   RtPbrSurveyEngine::RenderViewMode::IblBrdfLut,                      false, false},
        {"Shadow Mask",    RtPbrSurveyEngine::RenderViewMode::ShadowMask,                      true,  false},
        {"TLAS Debug",     RtPbrSurveyEngine::RenderViewMode::TlasDebug,                       true,  false},
        {"Ray Hit",        RtPbrSurveyEngine::RenderViewMode::ReflectionRayHit,                true,  true},
        {"Ray Distance",   RtPbrSurveyEngine::RenderViewMode::ReflectionRayDistance,           true,  true},
        {"Ray Normal",     RtPbrSurveyEngine::RenderViewMode::ReflectionRayNormal,             true,  true},
        {"Ray Albedo",     RtPbrSurveyEngine::RenderViewMode::ReflectionRayColor,              true,  true},
        {"Ray Material",   RtPbrSurveyEngine::RenderViewMode::ReflectionRayMaterial,           true,  true},
        {"Ray Emission",   RtPbrSurveyEngine::RenderViewMode::ReflectionRayEmission,           true,  true},
        {"Radiance",       RtPbrSurveyEngine::RenderViewMode::ReflectionRadiance,              true,  true},
        {"Ray Fade",       RtPbrSurveyEngine::RenderViewMode::ReflectionRayDistanceFade,       true,  true},
        {"Strength",       RtPbrSurveyEngine::RenderViewMode::ReflectionContributionStrength,  true,  true},
        {"Direct",         RtPbrSurveyEngine::RenderViewMode::ReflectionRadianceDirect,        true,  true},
        {"IBL Diffuse",    RtPbrSurveyEngine::RenderViewMode::ReflectionRadianceIblDiffuse,    true,  true},
        {"IBL Specular",   RtPbrSurveyEngine::RenderViewMode::ReflectionRadianceIblSpecular,   true,  true},
        {"Emissive Light", RtPbrSurveyEngine::RenderViewMode::ReflectionRadianceEmissive,      true,  true},
    };

    const char* RenderViewLabel(RtPbrSurveyEngine::RenderViewMode mode)
    {
        for (const RenderViewItem& item : kRenderViewItems)
        {
            if (item.mode == mode)
            {
                return item.label;
            }
        }

        return "Unknown";
    }

    bool IsRenderViewAvailable(const RenderViewItem& item, bool rayTracingSupported, bool reflectionEnabled)
    {
        return (!item.requiresRayTracing || rayTracingSupported) && (!item.requiresReflection || reflectionEnabled);
    }

    bool IsRenderViewModeAvailable(RtPbrSurveyEngine::RenderViewMode mode, bool rayTracingSupported, bool reflectionEnabled)
    {
        for (const RenderViewItem& item : kRenderViewItems)
        {
            if (item.mode == mode)
            {
                return IsRenderViewAvailable(item, rayTracingSupported, reflectionEnabled);
            }
        }

        return false;
    }

    void DrawFrameSummary(RtPbrSurvey::SceneRenderer& renderer)
    {
        const RtPbrSurvey::SceneRenderer::UiFrameContext context = renderer.GetUiFrameContext();
        ImGui::Text("FrameIndex: %d", context.frameIndex);
        if (context.cpuFrameTime > 0.0f)
        {
            ImGui::Text("CPU Frame: %.2f ms (%.1f FPS)", context.cpuFrameTime, 1000.0f / context.cpuFrameTime);
        }
        else
        {
            ImGui::TextUnformatted("CPU Frame: unavailable");
        }
        ImGui::Text("Ray Tracing: %s (Tier %ls, raw=%d)",
                    context.rayTracingSupported ? "Supported" : "Not supported",
                    context.rayTracingTierName,
                    context.rayTracingTierRaw);
        ImGui::Text("Temporal Upscaler: %s (Backend: %s, Status: %s)",
                    context.temporalUpscalerAvailable ? "Available" : "Unavailable",
                    context.temporalUpscalerBackendName,
                    context.temporalUpscalerStatusText);
    }

    void DrawTemporalUpscalerControls(RtPbrSurvey::SceneRenderer& renderer)
    {
        const RtPbrSurvey::SceneRenderer::UiFrameContext context = renderer.GetUiFrameContext();
        auto temporalUpscalerSettings = renderer.GetTemporalUpscalerSettings();
        bool changed = false;

        ImGui::BeginDisabled(!context.temporalUpscalerAvailable);
        changed |= ImGui::Checkbox("DLSS Enabled", &temporalUpscalerSettings.enabled);
        int temporalUpscalerQualityMode = static_cast<int>(temporalUpscalerSettings.qualityMode);
        if (ImGui::Combo("DLSS Quality",
                         &temporalUpscalerQualityMode,
                         "Native (DLAA)\0Ultra Quality\0Quality\0Balanced\0Performance\0Ultra Performance\0"))
        {
            temporalUpscalerSettings.qualityMode =
                static_cast<Engine::TemporalUpscalerQualityMode>(temporalUpscalerQualityMode);
            changed = true;
        }
        ImGui::EndDisabled();

        if (changed)
        {
            temporalUpscalerSettings.backend = Engine::TemporalUpscalerBackend::Streamline;
            renderer.SetTemporalUpscalerSettings(temporalUpscalerSettings);
        }
    }

    void DrawBackBufferControls(RtPbrSurvey::SceneRenderer& renderer)
    {
        std::array<float, 4> backBufferClearColor = renderer.GetBackBufferClearColor();
        if (ImGui::ColorEdit4("Background Color", backBufferClearColor.data()))
        {
            renderer.SetBackBufferClearColor(backBufferClearColor);
        }
    }

    void DrawToneMapControls(RtPbrSurvey::SceneRenderer& renderer)
    {
        RtPbrSurveyEngine::ToneMapParams toneMapParams = renderer.GetToneMapParams();
        bool changed = false;

        changed |= ImGui::RadioButton("None", &toneMapParams.operatorIndex, 0);
        ImGui::SameLine();
        changed |= ImGui::RadioButton("Reinhard", &toneMapParams.operatorIndex, 1);
        ImGui::SameLine();
        changed |= ImGui::RadioButton("ACES", &toneMapParams.operatorIndex, 2);
        changed |= ImGuiWidgets::SliderFloatWithControls("Exposure", &toneMapParams.exposure, 0.0f, 4.0f, 0.1f, 1.0f);
        changed |= ImGuiWidgets::SliderFloatWithControls("Paper White",
                                                         &toneMapParams.paperWhiteNits,
                                                         80.0f,
                                                         500.0f,
                                                         10.f,
                                                         300.0f,
                                                         "%.0f nits");
        changed |= ImGuiWidgets::SliderFloatWithControls("Display Max",
                                                         &toneMapParams.maxDisplayNits,
                                                         100.0f,
                                                         4000.0f,
                                                         50.f,
                                                         1000.0f,
                                                         "%.0f nits");

        if (changed)
        {
            renderer.SetToneMapParams(toneMapParams);
        }
    }

    void DrawShadowControls(RtPbrSurvey::SceneRenderer& renderer)
    {
        auto shadowSettings = renderer.GetShadowSettings();
        bool changed = false;

        changed |= ImGui::Checkbox("Shadow Enable", &shadowSettings.enabled);

        static constexpr ShadowPreset shadowPresets[] = {
            {"Hard Ref",     0.01f,  0.001f, false, 1,  0.0f,   0.0f},
            {"Low Bias",     0.002f, 0.001f, false, 1,  0.0f,   0.0f},
            {"Soft Compare", 0.01f,  0.001f, true,  8,  0.025f, 1.0f},
            {"Wide Soft",    0.01f,  0.001f, true,  16, 0.075f, 2.0f},
        };

        for (const ShadowPreset& preset : shadowPresets)
        {
            if (ImGui::SmallButton(preset.label))
            {
                shadowSettings.normalBias = preset.normalBias;
                shadowSettings.rayTMin = preset.rayTMin;
                shadowSettings.softShadowEnabled = preset.softShadowEnabled;
                shadowSettings.sampleCount = preset.sampleCount;
                shadowSettings.lightAngularRadius = preset.lightAngularRadius;
                shadowSettings.jitterStrength = preset.jitterStrength;
                changed = true;
            }
            ImGui::SameLine();
        }
        ImGui::NewLine();

        changed |= ImGuiWidgets::SliderFloatWithControls(
            "Normal Bias", &shadowSettings.normalBias, 0.0f, 0.1f, 0.001f, 0.01f);
        changed |= ImGuiWidgets::SliderFloatWithControls(
            "Ray TMin", &shadowSettings.rayTMin, 0.0f, 0.1f, 0.001f, 0.001f);
        changed |= ImGuiWidgets::SliderFloatWithControls(
            "Ray TMax", &shadowSettings.rayTMax, 1.0f, 10000.0f, 100.0f, 10000.0f);

        ImGui::Separator();
        changed |= ImGui::Checkbox("Soft Shadow Enable", &shadowSettings.softShadowEnabled);
        changed |= ImGuiWidgets::SliderIntWithControls("Sample Count", &shadowSettings.sampleCount, 1, 16, 1, 8);
        changed |= ImGuiWidgets::SliderFloatWithControls(
            "Light Angular Radius", &shadowSettings.lightAngularRadius, 0.0f, 0.1f, 0.001f, 0.1f, "%.4f");
        changed |= ImGuiWidgets::SliderFloatWithControls(
            "Jitter Strength", &shadowSettings.jitterStrength, 0.0f, 2.0f, 0.1f, 2.0f);

        if (changed)
        {
            renderer.SetShadowSettings(shadowSettings);
        }
    }

    void DrawHybridReflectionControls(RtPbrSurvey::SceneRenderer& renderer)
    {
        auto reflectionSettings = renderer.GetHybridReflectionSettings();
        bool changed = false;

        changed |= ImGui::Checkbox("Enabled", &reflectionSettings.enabled);

        ImGui::BeginDisabled(!reflectionSettings.enabled);
        changed |= ImGui::Checkbox("Hit Overlay", &reflectionSettings.hitOverlayEnabled);

        ImGui::BeginDisabled(!reflectionSettings.hitOverlayEnabled);
        changed |= ImGui::RadioButton("Overlay Cyan", &reflectionSettings.hitOverlayMode, 0);
        ImGui::SameLine();
        changed |= ImGui::RadioButton("Hit Position", &reflectionSettings.hitOverlayMode, 1);
        ImGui::SameLine();
        changed |= ImGui::RadioButton("Environment", &reflectionSettings.hitOverlayMode, 2);
        ImGui::SameLine();
        changed |= ImGui::RadioButton("Hit Attribute", &reflectionSettings.hitOverlayMode, 3);
        ImGui::SameLine();
        changed |= ImGui::RadioButton("Hit Albedo", &reflectionSettings.hitOverlayMode, 4);
        changed |= ImGuiWidgets::SliderFloatWithControls(
            "Hit Overlay Intensity", &reflectionSettings.hitOverlayIntensity, 0.0f, 1.0f, 0.05f, 0.2f);
        ImGui::EndDisabled();

        ImGui::BeginDisabled(reflectionSettings.hitOverlayMode != 3);
        changed |= ImGui::RadioButton("Attr Vertex Normal", &reflectionSettings.hitNormalSource, 0);
        ImGui::SameLine();
        changed |= ImGui::RadioButton("Attr Geometric Normal", &reflectionSettings.hitNormalSource, 1);
        ImGui::SameLine();
        changed |= ImGui::RadioButton("Attr MaterialId", &reflectionSettings.hitNormalSource, 2);
        changed |= ImGui::RadioButton("Attr Material Params", &reflectionSettings.hitNormalSource, 3);
        ImGui::SameLine();
        changed |= ImGui::RadioButton("Attr UV", &reflectionSettings.hitNormalSource, 4);
        ImGui::SameLine();
        changed |= ImGui::RadioButton("Attr Albedo Texture", &reflectionSettings.hitNormalSource, 5);
        ImGui::EndDisabled();

        changed |= ImGui::Checkbox("Reflection Contribution", &reflectionSettings.contributionEnabled);

        ImGui::BeginDisabled(!reflectionSettings.contributionEnabled);
        changed |= ImGuiWidgets::SliderFloatWithControls(
            "Contribution Intensity", &reflectionSettings.contributionIntensity, 0.0f, 2.0f, 0.05f, 0.25f);
        changed |= ImGuiWidgets::SliderFloatWithControls(
            "Contribution Max Distance", &reflectionSettings.contributionMaxDistance, 0.1f, 100.0f, 0.5f, 20.0f);
        ImGui::EndDisabled();

        changed |= ImGui::Checkbox("Material Gate", &reflectionSettings.materialGateEnabled);
        ImGui::BeginDisabled(!reflectionSettings.enabled || !reflectionSettings.materialGateEnabled);
        changed |= ImGuiWidgets::SliderFloatWithControls(
            "Max Roughness", &reflectionSettings.maxRoughness, 0.0f, 1.0f, 0.05f, 0.35f);
        changed |= ImGuiWidgets::SliderFloatWithControls(
            "Min Metallic", &reflectionSettings.minMetallic, 0.0f, 1.0f, 0.05f, 0.0f);
        ImGui::EndDisabled();
        ImGui::EndDisabled();

        if (changed)
        {
            renderer.SetHybridReflectionSettings(reflectionSettings);
        }
    }

    void DrawRenderViewControls(RtPbrSurvey::SceneRenderer& renderer)
    {
        const RtPbrSurvey::SceneRenderer::UiFrameContext context = renderer.GetUiFrameContext();
        const bool rayTracingSupported = context.rayTracingSupported;
        const bool reflectionEnabled = renderer.GetHybridReflectionSettings().enabled;
        RtPbrSurveyEngine::RenderingPath renderingPath = renderer.GetRenderingPath();
        RtPbrSurveyEngine::RenderViewMode renderViewMode = renderer.GetRenderViewMode();

        int renderingPathValue = static_cast<int>(renderingPath);
        bool pathChanged = false;
        pathChanged |= ImGui::RadioButton("Forward", &renderingPathValue, static_cast<int>(RtPbrSurveyEngine::RenderingPath::Forward));
        ImGui::SameLine();
        pathChanged |= ImGui::RadioButton("Deferred", &renderingPathValue, static_cast<int>(RtPbrSurveyEngine::RenderingPath::Deferred));
        if (pathChanged)
        {
            renderingPath = static_cast<RtPbrSurveyEngine::RenderingPath>(renderingPathValue);
            renderer.SetRenderingPath(renderingPath);
        }

        const bool deferredRendering = renderingPath == RtPbrSurveyEngine::RenderingPath::Deferred;
        ImGui::BeginDisabled(!deferredRendering);

        if (ImGui::BeginCombo("Render View", RenderViewLabel(renderViewMode)))
        {
            for (const RenderViewItem& item : kRenderViewItems)
            {
                const bool available = IsRenderViewAvailable(item, rayTracingSupported, reflectionEnabled);
                const bool selected = renderViewMode == item.mode;
                ImGui::BeginDisabled(!available);
                if (ImGui::Selectable(item.label, selected))
                {
                    renderViewMode = item.mode;
                    renderer.SetRenderViewMode(renderViewMode);
                }
                ImGui::EndDisabled();
                if (selected)
                {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        if (!deferredRendering || !IsRenderViewModeAvailable(renderViewMode, rayTracingSupported, reflectionEnabled))
        {
            renderViewMode = RtPbrSurveyEngine::RenderViewMode::LightPass;
            renderer.SetRenderViewMode(renderViewMode);
        }

        ImGui::EndDisabled();

        const bool lightPassView = deferredRendering && renderViewMode == RtPbrSurveyEngine::RenderViewMode::LightPass;
        bool lightingPassDebugGradient = renderer.GetLightingPassDebugGradient();
        ImGui::BeginDisabled(!lightPassView);
        if (ImGui::Checkbox("Debug LightPass Gradient", &lightingPassDebugGradient))
        {
            renderer.SetLightingPassDebugGradient(lightingPassDebugGradient);
        }
        ImGui::EndDisabled();
    }
}

namespace RtPbrSurvey
{
    void SceneRendererDebugUi::Draw(SceneRenderer& renderer, bool* open, const char* windowName)
    {
        ImGui::SetNextWindowSize(ImVec2(420, 520), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin(windowName, open))
        {
            ImGui::End();
            return;
        }

        DrawFrameSummary(renderer);
        DrawTemporalUpscalerControls(renderer);
        ImGui::Separator();

        if (ImGui::CollapsingHeader("Back Buffer", ImGuiTreeNodeFlags_DefaultOpen))
        {
            DrawBackBufferControls(renderer);
        }

        if (ImGui::CollapsingHeader("Tone Mapping", ImGuiTreeNodeFlags_DefaultOpen))
        {
            DrawToneMapControls(renderer);
        }

        if (ImGui::CollapsingHeader("RayQuery Shadow", ImGuiTreeNodeFlags_DefaultOpen))
        {
            DrawShadowControls(renderer);
        }

        if (ImGui::CollapsingHeader("Hybrid Reflection", ImGuiTreeNodeFlags_DefaultOpen))
        {
            DrawHybridReflectionControls(renderer);
        }

        if (ImGui::CollapsingHeader("Render View", ImGuiTreeNodeFlags_DefaultOpen))
        {
            DrawRenderViewControls(renderer);
        }

        ImGui::End();
    }
}
