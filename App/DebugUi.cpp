#include "stdafx.h"
#include "DebugUi.h"
#include "SceneSelectUi.h"
#include "RtPbrSurveyApp.h"
#include "../ImGuiWidgets.h"

#include <imgui.h>

void RunStagedAllocatorTests(ID3D12Device* device);

namespace
{

struct CameraSpeedPreset
{
    const char* label;
    float multiplier;
};

const char* EnvironmentSourceLabel(Engine::EnvironmentSource source)
{
    switch (source)
    {
        case Engine::EnvironmentSource::AssetHdr:
            return "Asset HDR";
        case Engine::EnvironmentSource::ProceduralStudio:
            return "Procedural Studio";
        case Engine::EnvironmentSource::ProceduralSun:
            return "Procedural Sun";
        case Engine::EnvironmentSource::ProceduralColorPanels:
            return "Procedural Color Panels";
        case Engine::EnvironmentSource::ProceduralHorizon:
            return "Procedural Horizon";
        default:
            return "Unknown";
    }
}

const char* RenderViewDescription(RtPbrSurveyEngine::RenderViewMode mode)
{
    using RenderViewMode = RtPbrSurveyEngine::RenderViewMode;

    switch (mode)
    {
        case RenderViewMode::LightPass:
            return "Final lit scene after deferred lighting and enabled reflection composite.";
        case RenderViewMode::GBufferAlbedo:
            return "GBuffer albedo: base surface color written by the material pass.";
        case RenderViewMode::GBufferNormal:
            return "GBuffer normal: encoded visible-surface normal decoded for inspection.";
        case RenderViewMode::GBufferMaterial:
            return "GBuffer material id / material data debug view.";
        case RenderViewMode::GBufferMotionVector:
            return "GBuffer motion vector: screen-space motion data for temporal effects.";
        case RenderViewMode::GBufferPBRParams:
            return "GBuffer PBR params: metallic, roughness, ambient occlusion, and related material terms.";
        case RenderViewMode::GBufferEmissive:
            return "GBuffer emissive: material emission before lighting composite.";
        case RenderViewMode::Depth:
            return "Depth buffer debug view.";
        case RenderViewMode::ReflectionDirection:
            return "Visible-surface reflection direction used for specular IBL and ray setup.";
        case RenderViewMode::ViewDirection:
            return "View direction from visible surface toward the camera.";
        case RenderViewMode::WorldPosition:
            return "Reconstructed visible-surface world position.";
        case RenderViewMode::NdotV:
            return "NdotV: cosine between visible normal and view direction.";
        case RenderViewMode::IblEnvironment:
            return "Environment cubemap sample used by IBL debug inspection.";
        case RenderViewMode::IblDiffuseIrradiance:
            return "Diffuse irradiance cubemap used for ambient diffuse IBL.";
        case RenderViewMode::IblSpecularPrefilter:
            return "Specular prefilter cubemap. Use Prefilter Mip to inspect roughness levels.";
        case RenderViewMode::IblBrdfLut:
            return "BRDF integration LUT used by split-sum specular IBL.";
        case RenderViewMode::ReflectionRayHit:
            return "Reflection ray hit mask. White means the ray hit scene geometry.";
        case RenderViewMode::ReflectionRayDistance:
            return "Reflection hit distance, normalized for debug display.";
        case RenderViewMode::ReflectionRayNormal:
            return "Normal at the reflection hit point, reconstructed from ray query hit data.";
        case RenderViewMode::ReflectionRayColor:
            return "ReflectionRayColor payload: hit material color. This is not reflected radiance.";
        case RenderViewMode::ReflectionRayDistanceFade:
            return "Distance fade applied to the provisional reflection contribution.";
        case RenderViewMode::ReflectionContributionStrength:
            return "Scalar reflection contribution strength before visible-surface Fresnel is applied.";
        case RenderViewMode::ShadowMask:
            return "Ray query shadow mask. Darker pixels receive less direct light.";
        case RenderViewMode::TlasDebug:
            return "TLAS ray query debug view for acceleration-structure hit inspection.";
        case RenderViewMode::ReflectionRayMaterial:
            return "ReflectionRayMaterial payload: R=metallic, G=roughness, B=unlit flag at the hit point.";
        case RenderViewMode::ReflectionRadiance:
            return "ReflectionRadiance is the reflection radiance buffer before LightPass.\n"
                   "This view shows ReflectionEvaluatePass output, not the final reflected color.\n"
                   "Current value is provisional: hit material color with distance fade and hit roughness attenuation.\n"
                   "LightPass applies the visible-surface Fresnel term before adding it.";
        default:
            return nullptr;
    }
}

void DrawRenderViewDescription(RtPbrSurveyEngine::RenderViewMode mode)
{
    const char* description = RenderViewDescription(mode);
    if (description == nullptr || description[0] == '\0')
    {
        return;
    }

    ImGui::Separator();
    ImGui::TextWrapped("%s", description);
}

void ApplyEnvironmentPreset(Engine::ProceduralEnvironmentSettings& settings, Engine::EnvironmentSource source)
{
    settings = {};
    settings.source = source;

    switch (source)
    {
        case Engine::EnvironmentSource::ProceduralStudio:
            settings.skyColor = {0.50f, 0.52f, 0.54f};
            settings.groundColor = {0.16f, 0.16f, 0.15f};
            settings.lightColor = {1.0f, 0.98f, 0.92f};
            settings.lightDirection = {0.25f, 0.85f, 0.35f};
            settings.backgroundIntensity = 0.35f;
            settings.lightIntensity = 4.0f;
            settings.lightSize = 0.22f;
            settings.fillIntensity = 0.08f;
            break;
        case Engine::EnvironmentSource::ProceduralSun:
            settings.skyColor = {0.25f, 0.43f, 0.75f};
            settings.groundColor = {0.09f, 0.075f, 0.055f};
            settings.lightColor = {1.0f, 0.82f, 0.52f};
            settings.lightDirection = {0.22f, 0.72f, 0.66f};
            settings.backgroundIntensity = 0.20f;
            settings.lightIntensity = 32.0f;
            settings.lightSize = 0.035f;
            settings.fillIntensity = 0.03f;
            break;
        case Engine::EnvironmentSource::ProceduralColorPanels:
            settings.skyColor = {0.02f, 0.02f, 0.025f};
            settings.groundColor = {0.015f, 0.015f, 0.015f};
            settings.lightColor = {1.0f, 1.0f, 1.0f};
            settings.lightDirection = {0.35f, 0.75f, 0.25f};
            settings.backgroundIntensity = 0.05f;
            settings.lightIntensity = 0.0f;
            settings.lightSize = 0.12f;
            settings.fillIntensity = 0.02f;
            settings.colorPanelIntensity = 3.5f;
            break;
        case Engine::EnvironmentSource::ProceduralHorizon:
            settings.skyColor = {0.34f, 0.50f, 0.86f};
            settings.groundColor = {0.18f, 0.15f, 0.10f};
            settings.lightColor = {1.0f, 0.86f, 0.62f};
            settings.lightDirection = {0.1f, 0.08f, 0.99f};
            settings.backgroundIntensity = 0.45f;
            settings.lightIntensity = 5.0f;
            settings.lightSize = 0.12f;
            settings.fillIntensity = 0.03f;
            settings.horizonSharpness = 0.035f;
            break;
        case Engine::EnvironmentSource::AssetHdr:
        default:
            break;
    }
}

} // namespace

namespace App
{

void DrawDebugUi(RtPbrSurveyApp& app, const RtPbrSurveyEngine::UiFrameContext& context)
{
    using RenderingPath = RtPbrSurveyEngine::RenderingPath;
    using RenderViewMode = RtPbrSurveyEngine::RenderViewMode;

    if (app.m_appMode == RtPbrSurveyApp::AppMode::SceneSelect)
    {
        App::DrawSceneSelectUi(app);
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(400, 140), ImGuiCond_FirstUseEver);
    ImGui::Begin("Debug");

    Engine::SampleScene& loadedScene = app.LoadedScene();
    Engine::SceneMesh& sceneMesh = loadedScene.GetMesh();

    ImGui::Text("Hello ImGui");
    ImGui::Text("Scene: %s", loadedScene.Name());
    ImGui::Text("Loaded Scene Index: %d", app.m_loadedSceneIndex);
    ImGui::Text("FrameIndex: %d", context.frameIndex);
    ImGui::Text("Ray Tracing: %s (Tier %ls, raw=%d)",
                context.rayTracingSupported ? "Supported" : "Not supported",
                context.rayTracingTierName,
                context.rayTracingTierRaw);
    if (ImGui::Button("Close Scene"))
    {
        app.CloseRunningScene();
        ImGui::End();
        return;
    }
    if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGuiWidgets::SliderFloatWithControls("FovH", &loadedScene.GetScene().camera.fov, 20.f, 150.f, 5.f, 60.f);
        int cameraMode = static_cast<int>(app.m_cameraMode);
        if (ImGui::Combo("Mode", &cameraMode, "FreeLook\0Arcball\0"))
        {
            app.m_cameraMode = static_cast<RtPbrSurveyApp::CameraMode>(cameraMode);
            if (app.m_cameraMode == RtPbrSurveyApp::CameraMode::Arcball)
            {
                app.InitObjectViewerFromCamera();
            }
        }

        {
            static constexpr CameraSpeedPreset presets[] = {
                {"1/10", 0.1f},
                {"1/5",  0.2f},
                {"1/3",  0.333f},
                {"1/2",  0.5f},
                {"x1",   1.0f},
                {"x2",   2.0f},
                {"x3",   3.0f},
                {"x5",   5.0f},
                {"x10", 10.0f},
            };
            for (const auto& p : presets)
            {
                const bool isActive = (app.m_cameraSpeedMultiplier == p.multiplier);
                if (isActive)
                    ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
                if (ImGui::SmallButton(p.label))
                    app.m_cameraSpeedMultiplier = p.multiplier;
                if (isActive)
                    ImGui::PopStyleColor();
                ImGui::SameLine();
            }
            ImGui::NewLine();
            ImGuiWidgets::SliderFloatWithControls("NearZ", &loadedScene.GetScene().camera.nearZ, 0.01f, 10.0f, 0.01f, 0.1f);
            ImGuiWidgets::SliderFloatWithControls("FarZ", &loadedScene.GetScene().camera.farZ, 10.0f, 100000.0f, 100.0f, 10000.0f);
        }
    }
    if (ImGui::CollapsingHeader("Scene", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGuiWidgets::SliderIntWithControls("Display Instance Count", &app.m_displayInstanceCount, 0,
                                             loadedScene.MaxDisplayInstanceCount(), 1, 0);
        loadedScene.SetDisplayInstanceCount(app.m_displayInstanceCount);
        ImGuiWidgets::SliderFloatWithControls("Mesh Scale", &app.m_meshScale, 0.1f, 2.0f, 0.05f, 0.5f);
        ImGui::ColorEdit4("Background Color", app.m_backBufferClearColor.data());
    }
    if (ImGui::CollapsingHeader("PBR Lighting", ImGuiTreeNodeFlags_DefaultOpen))
    {
        {
            static constexpr float defaultDir[] = {0.0f, 1.0f, -1.0f};
            ImGuiWidgets::SliderFloat3WithControls("Light Direction", &app.m_lightingParams.lightDirection.x, -1.0f, 1.0f,
                                                     0.05f, defaultDir);
        }
        ImGui::SameLine();
        ImGuiWidgets::SliderFloatWithControls("Direct Light Intensity", &app.m_lightingParams.diffuseIntensity, 0.0f, 4.0f,
                                               0.1f, 1.0f);
        ImGui::ColorEdit3("Light Color", &app.m_lightingParams.lightColor.x);
        ImGui::Checkbox("IBL Enabled", &app.m_iblEnabled);
        ImGui::BeginDisabled(!app.m_iblEnabled);
        ImGuiWidgets::SliderFloatWithControls("IBL Intensity", &app.m_lightingParams.iblIntensity, 0.0f, 2.0f, 0.05f, 1.0f);
        ImGui::Checkbox("Diffuse IBL", &app.m_lightingParams.diffuseIblEnabled);
        ImGui::SameLine();
        ImGui::Checkbox("Specular IBL", &app.m_lightingParams.specularIblEnabled);
        ImGui::EndDisabled();
        ImGui::Checkbox("Direct Light", &app.m_lightingParams.directLightEnabled);
        ImGui::SameLine();
        ImGui::Checkbox("Emissive", &app.m_lightingParams.emissiveEnabled);
    }
    if (ImGui::CollapsingHeader("Environment Map", ImGuiTreeNodeFlags_DefaultOpen))
    {
        bool environmentApplyRequested = false;
        int environmentSource = static_cast<int>(app.m_environmentSettings.source);
        if (ImGui::Combo("Source",
                         &environmentSource,
                         "Asset HDR\0Procedural Studio\0Procedural Sun\0Procedural Color Panels\0Procedural Horizon\0"))
        {
            ApplyEnvironmentPreset(app.m_environmentSettings, static_cast<Engine::EnvironmentSource>(environmentSource));
            environmentApplyRequested = true;
        }
        ImGui::SameLine();
        ImGui::Text("%s", EnvironmentSourceLabel(app.m_environmentSettings.source));
        if (app.m_environmentSettings.source != Engine::EnvironmentSource::AssetHdr)
        {
            ImGui::SameLine();
            ImGui::Checkbox("Auto Update", &app.m_environmentAutoUpdate);
            environmentApplyRequested |= ImGui::ColorEdit3("Sky Color", &app.m_environmentSettings.skyColor.x);
            environmentApplyRequested |= ImGui::ColorEdit3("Ground Color", &app.m_environmentSettings.groundColor.x);
            const bool colorPanels = app.m_environmentSettings.source == Engine::EnvironmentSource::ProceduralColorPanels;
            if (!colorPanels)
            {
                environmentApplyRequested |= ImGui::ColorEdit3("Env Light Color", &app.m_environmentSettings.lightColor.x);
                static constexpr float defaultEnvLightDir[] = {0.35f, 0.75f, 0.25f};
                if (ImGuiWidgets::SliderFloat3WithControls("Env Light Direction", &app.m_environmentSettings.lightDirection.x,
                                                        -1.0f, 1.0f, 0.05f, defaultEnvLightDir))
                {
                    environmentApplyRequested = true;
                }
            }
            if (ImGuiWidgets::SliderFloatWithControls("Env Background",
                                                      &app.m_environmentSettings.backgroundIntensity,
                                                      0.0f,
                                                      4.0f,
                                                      0.05f,
                                                      0.6f))
            {
                environmentApplyRequested = true;
            }
            if (!colorPanels)
            {
                if (ImGuiWidgets::SliderFloatWithControls("Env Light Intensity",
                                                          &app.m_environmentSettings.lightIntensity,
                                                          0.0f,
                                                          40.0f,
                                                          0.5f,
                                                          6.0f))
                {
                    environmentApplyRequested = true;
                }
                if (ImGuiWidgets::SliderFloatWithControls("Env Light Size",
                                                          &app.m_environmentSettings.lightSize,
                                                          0.01f,
                                                          0.8f,
                                                          0.01f,
                                                          0.12f))
                {
                    environmentApplyRequested = true;
                }
            }
            if (ImGuiWidgets::SliderFloatWithControls(
                "Env Fill", &app.m_environmentSettings.fillIntensity, 0.0f, 2.0f, 0.05f, 0.12f))
            {
                environmentApplyRequested = true;
            }
            if (colorPanels)
            {
                if (ImGuiWidgets::SliderFloatWithControls("Color Panel Intensity",
                                                          &app.m_environmentSettings.colorPanelIntensity,
                                                          0.0f,
                                                          8.0f,
                                                          0.1f,
                                                          1.5f))
                {
                    environmentApplyRequested = true;
                }
            }
            if (app.m_environmentSettings.source == Engine::EnvironmentSource::ProceduralHorizon)
            {
                if (ImGuiWidgets::SliderFloatWithControls("Horizon Width",
                                                          &app.m_environmentSettings.horizonSharpness,
                                                          0.01f,
                                                          0.5f,
                                                          0.01f,
                                                          0.08f))
                {
                    environmentApplyRequested = true;
                }
            }
        }
        ImGui::Checkbox("Show Skybox", &app.m_lightingParams.skyboxEnabled);
        ImGui::Checkbox("Skybox Preview", &app.m_lightingParams.skyboxPreview);
        ImGui::BeginDisabled(!app.m_lightingParams.skyboxPreview);
        ImGuiWidgets::SliderFloatWithControls("Skybox Preview Exposure", &app.m_lightingParams.skyboxPreviewExposure, 0.0f,
                                              2.0f, 0.05f, 1.0f);
        ImGui::EndDisabled();
        if (environmentApplyRequested)
        {
            app.m_environmentReloadPending = true;
        }
        if (app.m_environmentReloadPending && app.m_environmentAutoUpdate && !ImGui::IsAnyItemActive())
        {
            app.m_engine.ReloadEnvironmentResources(app.m_environmentSettings);
            app.m_environmentReloadPending = false;
        }
    }

    if (!sceneMesh.materials.empty())
    {
        if (ImGui::CollapsingHeader("Material Controls", ImGuiTreeNodeFlags_DefaultOpen))
        {
            const int materialCount = static_cast<int>(sceneMesh.materials.size());
            if (app.m_selectedMaterialIndex >= materialCount)
            {
                app.m_selectedMaterialIndex = materialCount - 1;
            }
            ImGuiWidgets::SliderIntWithControls("Material", &app.m_selectedMaterialIndex, 0, materialCount - 1, 1, 0);

            Engine::SceneMaterial& material = sceneMesh.materials[app.m_selectedMaterialIndex];
            bool materialChanged = false;
            materialChanged |= ImGuiWidgets::SliderFloatWithControls("Roughness", &material.roughnessFactor, 0.04f, 1.0f,
                                                                       0.02f, 0.5f);
            materialChanged |= ImGuiWidgets::SliderFloatWithControls("Metallic", &material.metallicFactor, 0.0f, 1.0f,
                                                                       0.05f, 0.0f);
            materialChanged |= ImGuiWidgets::SliderFloatWithControls("Indirect Occlusion",
                                                                       &material.ambientOcclusionFactor, 0.0f, 1.0f,
                                                                       0.05f, 1.0f);
            materialChanged |= ImGuiWidgets::SliderFloatWithControls("Emissive Luminance", &material.emissiveScale, 0.0f,
                                                                       4.0f, 0.1f, 1.0f);

            const float f0 = 0.04f * (1.0f - material.metallicFactor) + material.metallicFactor;
            ImGui::Text("Specular F0: %.2f", f0);

            if (materialChanged)
            {
                RtPbrSurveyEngine::MaterialParams params = {};
                params.roughnessFactor = material.roughnessFactor;
                params.metallicFactor = material.metallicFactor;
                params.ambientOcclusionFactor = material.ambientOcclusionFactor;
                params.emissiveScale = material.emissiveScale;
                app.m_engine.SetMaterialParams(static_cast<UINT>(app.m_selectedMaterialIndex), params);
            }
        }
    }

    if (ImGui::CollapsingHeader("Tone Mapping", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::RadioButton("None", &app.m_toneMapParams.operatorIndex, 0);
        ImGui::SameLine();
        ImGui::RadioButton("Reinhard", &app.m_toneMapParams.operatorIndex, 1);
        ImGui::SameLine();
        ImGui::RadioButton("ACES", &app.m_toneMapParams.operatorIndex, 2);
        ImGuiWidgets::SliderFloatWithControls("Exposure", &app.m_toneMapParams.exposure, 0.0f, 4.0f, 0.1f, 1.0f);
        ImGuiWidgets::SliderFloatWithControls("Paper White", &app.m_toneMapParams.paperWhiteNits, 80.0f, 500.0f, 10.f,
                                              300.0f, "%.0f nits");
        ImGuiWidgets::SliderFloatWithControls("Display Max", &app.m_toneMapParams.maxDisplayNits, 100.0f, 4000.0f, 50.f,
                                              1000.0f, "%.0f nits");
    }

    if (ImGui::CollapsingHeader("RayQuery Shadow", ImGuiTreeNodeFlags_DefaultOpen))
    {
        auto shadowSettings = app.m_engine.GetShadowSettings();
        bool changed = false;

        changed |= ImGui::Checkbox("Shadow Enable", &shadowSettings.enabled);

        changed |= ImGuiWidgets::SliderFloatWithControls(
            "Normal Bias", &shadowSettings.normalBias, 0.0f, 0.1f, 0.001f, 0.01f);

        changed |= ImGuiWidgets::SliderFloatWithControls(
            "Ray TMin", &shadowSettings.rayTMin, 0.0f, 0.1f, 0.001f, 0.001f);

        changed |= ImGuiWidgets::SliderFloatWithControls(
            "Ray TMax", &shadowSettings.rayTMax, 1.0f, 10000.0f, 100.0f, 10000.0f);

        ImGui::Separator();
        changed |= ImGui::Checkbox("Soft Shadow Enable", &shadowSettings.softShadowEnabled);

        changed |= ImGuiWidgets::SliderIntWithControls(
            "Sample Count", &shadowSettings.sampleCount, 1, 16, 1, 8);

        changed |= ImGuiWidgets::SliderFloatWithControls(
            "Light Angular Radius", &shadowSettings.lightAngularRadius, 0.0f, 0.1f, 0.001f, 0.1f, "%.4f");

        changed |= ImGuiWidgets::SliderFloatWithControls(
            "Jitter Strength", &shadowSettings.jitterStrength, 0.0f, 2.0f, 0.1f, 2.0f);

        if (changed)
        {
            app.m_engine.SetShadowSettings(shadowSettings);
        }
    }

    if (ImGui::CollapsingHeader("Hybrid Reflection", ImGuiTreeNodeFlags_DefaultOpen))
    {
        auto reflectionSettings = app.m_engine.GetHybridReflectionSettings();
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
            app.m_engine.SetHybridReflectionSettings(reflectionSettings);
        }
    }

    if (ImGui::CollapsingHeader("Debug", ImGuiTreeNodeFlags_DefaultOpen))
    {
        int renderingPath = static_cast<int>(app.m_renderingPath);
        ImGui::RadioButton("Forward", &renderingPath, static_cast<int>(RenderingPath::Forward));
        ImGui::SameLine();
        ImGui::RadioButton("Deferred", &renderingPath, static_cast<int>(RenderingPath::Deferred));
        app.m_renderingPath = static_cast<RenderingPath>(renderingPath);

        const bool deferredRendering = app.m_renderingPath == RenderingPath::Deferred;
        int renderViewMode = static_cast<int>(app.m_renderViewMode);
        ImGui::BeginDisabled(!deferredRendering);
        ImGui::RadioButton("Lit", &renderViewMode, static_cast<int>(RenderViewMode::LightPass));
        ImGui::RadioButton("Albedo", &renderViewMode, static_cast<int>(RenderViewMode::GBufferAlbedo));
        ImGui::SameLine();
        ImGui::RadioButton("Normal", &renderViewMode, static_cast<int>(RenderViewMode::GBufferNormal));
        ImGui::SameLine();
        ImGui::RadioButton("Material", &renderViewMode, static_cast<int>(RenderViewMode::GBufferMaterial));
        ImGui::RadioButton("MotionVector", &renderViewMode, static_cast<int>(RenderViewMode::GBufferMotionVector));
        ImGui::SameLine();
        ImGui::RadioButton("PBR Params", &renderViewMode, static_cast<int>(RenderViewMode::GBufferPBRParams));
        ImGui::SameLine();
        ImGui::RadioButton("Emissive##RenderView", &renderViewMode, static_cast<int>(RenderViewMode::GBufferEmissive));
        ImGui::SameLine();
        ImGui::RadioButton("Depth", &renderViewMode, static_cast<int>(RenderViewMode::Depth));
        ImGui::SameLine();
        ImGui::RadioButton("ReflectionDir", &renderViewMode, static_cast<int>(RenderViewMode::ReflectionDirection));
        ImGui::RadioButton("ViewDir", &renderViewMode, static_cast<int>(RenderViewMode::ViewDirection));
        ImGui::SameLine();
        ImGui::RadioButton("WorldPos", &renderViewMode, static_cast<int>(RenderViewMode::WorldPosition));
        ImGui::SameLine();
        ImGui::RadioButton("NdotV", &renderViewMode, static_cast<int>(RenderViewMode::NdotV));
        ImGui::RadioButton("IBL Env", &renderViewMode, static_cast<int>(RenderViewMode::IblEnvironment));
        ImGui::SameLine();
        ImGui::RadioButton("IBL Irradiance", &renderViewMode, static_cast<int>(RenderViewMode::IblDiffuseIrradiance));
        ImGui::SameLine();
        ImGui::RadioButton("IBL Prefilter", &renderViewMode, static_cast<int>(RenderViewMode::IblSpecularPrefilter));
        ImGui::SameLine();
        ImGui::RadioButton("IBL BRDF LUT", &renderViewMode, static_cast<int>(RenderViewMode::IblBrdfLut));
        ImGui::BeginDisabled(!context.rayTracingSupported);
        ImGui::RadioButton("Shadow Mask", &renderViewMode, static_cast<int>(RenderViewMode::ShadowMask));
        ImGui::SameLine();
        ImGui::RadioButton("TLAS Debug", &renderViewMode, static_cast<int>(RenderViewMode::TlasDebug));
        const bool reflectionDebugEnabled = app.m_engine.GetHybridReflectionSettings().enabled;
        ImGui::BeginDisabled(!reflectionDebugEnabled);
        ImGui::RadioButton("Reflection Hit", &renderViewMode, static_cast<int>(RenderViewMode::ReflectionRayHit));
        ImGui::SameLine();
        ImGui::RadioButton("Reflection Distance", &renderViewMode, static_cast<int>(RenderViewMode::ReflectionRayDistance));
        ImGui::SameLine();
        ImGui::RadioButton("Reflection Normal", &renderViewMode, static_cast<int>(RenderViewMode::ReflectionRayNormal));
        ImGui::RadioButton("Reflection Material Color", &renderViewMode, static_cast<int>(RenderViewMode::ReflectionRayColor));
        ImGui::SameLine();
        ImGui::RadioButton(
            "Reflection Material Params", &renderViewMode, static_cast<int>(RenderViewMode::ReflectionRayMaterial));
        ImGui::SameLine();
        ImGui::RadioButton("Reflection Radiance", &renderViewMode, static_cast<int>(RenderViewMode::ReflectionRadiance));
        ImGui::RadioButton("Reflection Fade", &renderViewMode, static_cast<int>(RenderViewMode::ReflectionRayDistanceFade));
        ImGui::SameLine();
        ImGui::RadioButton("Reflection Strength", &renderViewMode, static_cast<int>(RenderViewMode::ReflectionContributionStrength));
        ImGui::EndDisabled();
        ImGui::EndDisabled();
        app.m_renderViewMode = static_cast<RenderViewMode>(renderViewMode);
        if (!context.rayTracingSupported &&
            (app.m_renderViewMode == RenderViewMode::ShadowMask || app.m_renderViewMode == RenderViewMode::TlasDebug ||
             app.m_renderViewMode == RenderViewMode::ReflectionRayHit ||
             app.m_renderViewMode == RenderViewMode::ReflectionRayDistance ||
             app.m_renderViewMode == RenderViewMode::ReflectionRayNormal ||
             app.m_renderViewMode == RenderViewMode::ReflectionRayColor ||
             app.m_renderViewMode == RenderViewMode::ReflectionRayMaterial ||
             app.m_renderViewMode == RenderViewMode::ReflectionRadiance ||
             app.m_renderViewMode == RenderViewMode::ReflectionRayDistanceFade ||
             app.m_renderViewMode == RenderViewMode::ReflectionContributionStrength))
        {
            app.m_renderViewMode = RenderViewMode::LightPass;
        }
        if (!reflectionDebugEnabled &&
            (app.m_renderViewMode == RenderViewMode::ReflectionRayHit ||
             app.m_renderViewMode == RenderViewMode::ReflectionRayDistance ||
             app.m_renderViewMode == RenderViewMode::ReflectionRayNormal ||
             app.m_renderViewMode == RenderViewMode::ReflectionRayColor ||
             app.m_renderViewMode == RenderViewMode::ReflectionRayMaterial ||
             app.m_renderViewMode == RenderViewMode::ReflectionRadiance ||
             app.m_renderViewMode == RenderViewMode::ReflectionRayDistanceFade ||
             app.m_renderViewMode == RenderViewMode::ReflectionContributionStrength))
        {
            app.m_renderViewMode = RenderViewMode::LightPass;
        }
        DrawRenderViewDescription(app.m_renderViewMode);
        const bool iblDebugView = app.m_renderViewMode == RenderViewMode::IblEnvironment ||
            app.m_renderViewMode == RenderViewMode::IblDiffuseIrradiance ||
            app.m_renderViewMode == RenderViewMode::IblSpecularPrefilter;
        ImGui::BeginDisabled(!iblDebugView);
        ImGuiWidgets::SliderFloatWithControls(
            "IBL Cube Exposure", &app.m_lightingParams.iblDebugExposure, 0.01f, 2.0f, 0.05f, 0.25f);
        ImGui::EndDisabled();
        ImGui::BeginDisabled(app.m_renderViewMode != RenderViewMode::IblSpecularPrefilter);
        ImGuiWidgets::SliderFloatWithControls("Prefilter Mip",
                                              &app.m_lightingParams.iblDebugMip,
                                              0.0f,
                                              static_cast<float>(RtPbrSurveyEngine::kSpecularPrefilterMipCount - 1),
                                              1.0f,
                                              0.0f);
        ImGui::EndDisabled();
        ImGui::EndDisabled();

        if (!deferredRendering)
        {
            app.m_renderViewMode = RenderViewMode::LightPass;
        }

        const bool lightPassView = deferredRendering && app.m_renderViewMode == RenderViewMode::LightPass;
        ImGui::BeginDisabled(!lightPassView);
        ImGui::Checkbox("Debug LightPass Gradient", &app.m_lightingPassDebugGradient);
        ImGui::EndDisabled();

        if (lightPassView && app.m_lightingPassDebugGradient)
        {
            if (ImGui::Button("Validate HDR Gradient"))
            {
                app.m_requestHdrDump = true;
            }
        }

        ImGui::Separator();
        if (ImGui::Button("Run Descriptor Allocator Tests"))
        {
            RunStagedAllocatorTests(app.m_graphicsDevice.Device());
        }
    }

    if (ImGui::CollapsingHeader("Pixel Pick (Ctrl+Click)", ImGuiTreeNodeFlags_DefaultOpen))
    {
        const auto& pick = app.m_engine.GetPixelPickResult();
        if (pick.valid)
        {
            ImGui::Text("Screen / Depth / Material");
            ImGui::Text("  Screen: (%d, %d)  Mat ID: %u", pick.screenX, pick.screenY, pick.materialId);
            ImGui::Text("  Depth (NDC): %.4f", pick.depthNdc);
            ImGui::Separator();
            ImGui::Text("World Vectors");
            ImGui::Text("  World Pos:   (%.3f, %.3f, %.3f)", pick.worldPos.x, pick.worldPos.y, pick.worldPos.z);
            ImGui::Text("  Normal:      (%.3f, %.3f, %.3f)", pick.normal.x, pick.normal.y, pick.normal.z);
            ImGui::Text("  View Dir:    (%.3f, %.3f, %.3f)", pick.viewDir.x, pick.viewDir.y, pick.viewDir.z);
            ImGui::Text("  Reflect Dir: (%.3f, %.3f, %.3f)", pick.reflectionDir.x, pick.reflectionDir.y,
                         pick.reflectionDir.z);
            ImGui::Separator();
            ImGui::Text("GBuffer Material");
            ImGui::Text("  Albedo:      (%.3f, %.3f, %.3f, %.3f)", pick.albedo.x, pick.albedo.y, pick.albedo.z,
                         pick.albedo.w);
            ImGui::Text("  Metallic:    %.3f", pick.metallic);
            ImGui::Text("  Roughness:   %.3f", pick.roughness);
            ImGui::Text("  AO:          %.3f", pick.ambientOcclusion);
            ImGui::Text("  Emissive:    (%.3f, %.3f, %.3f)", pick.emissive.x, pick.emissive.y, pick.emissive.z);
            ImGui::Separator();
            ImGui::Text("Shadow Mask:   %.3f", pick.shadowMask);
            ImGui::Separator();
            if (ImGui::TreeNodeEx("Specular Reflection", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::Text("Status: %s", pick.reflectionHit ? "Hit" : "Miss");
                if (pick.reflectionHit)
                {
                    ImGui::Text("Dist: %.3f", pick.reflectionHitResult);
                    ImGui::Text("Pos:  (%.3f, %.3f, %.3f)",
                                pick.reflectionHitWorldPos.x,
                                pick.reflectionHitWorldPos.y,
                                pick.reflectionHitWorldPos.z);
                }
                ImGui::TreePop();
            }

        }
        else
        {
            ImGui::Text("Ctrl+Click on viewport to pick a pixel.");
        }
    }

    if (ImGui::CollapsingHeader("Specular Debug Lines", ImGuiTreeNodeFlags_DefaultOpen))
    {
        auto debugLines = app.m_engine.GetSpecularDebugLineSettings();

        ImGui::Checkbox("Enable Debug Lines", &debugLines.enabled);
        ImGui::SliderFloat("Line Length", &debugLines.lineLength, 0.1f, 5.0f, "%.1f");

        ImGui::Checkbox("View Ray (yellow)", &debugLines.showViewRay);
        ImGui::Checkbox("Normal (blue)", &debugLines.showNormal);
        ImGui::Checkbox("Reflection (hit magenta / miss gray)", &debugLines.showReflection);

        ImGui::Separator();
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "---");
        ImGui::SameLine();
        ImGui::Text("View Ray");
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.0f, 0.0f, 1.0f, 1.0f), "---");
        ImGui::SameLine();
        ImGui::Text("Normal");
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.0f, 1.0f, 1.0f), "---");
        ImGui::SameLine();
        ImGui::Text("Reflection");

        app.m_engine.SetSpecularDebugLineSettings(debugLines);
    }

    if (ImGui::CollapsingHeader("Scene Config"))
    {
        // Status message (auto-clears after ~2 seconds)
        static int statusFrames = 0;
        static const char* statusMsg = "";

        // Source indicator
        const auto source = app.m_sceneConfig.ActiveSource(
            app.LoadedScene().Name());
        const char* sourceLabels[] = { "Code defaults", "Default config", "User config" };
        ImGui::Text("Source: %s", sourceLabels[static_cast<int>(source)]);

        // User config path (compact)
        ImGui::Text("Path: %s", app.m_sceneConfig.UserConfigPath().c_str());

        // Row 1: Save / Load
        if (ImGui::Button("Save Current"))
        {
            app.m_sceneConfig.SaveCurrentScene(
                app.m_loadedSceneIndex, app, app.m_engine, app.LoadedScene());
            statusMsg = "Saved.";
            statusFrames = 120;
        }
        ImGui::SameLine();
        if (ImGui::Button("Load Defaults"))
        {
            app.m_sceneConfig.LoadDefaultsForScene(
                app.m_loadedSceneIndex, app, app.m_engine, app.LoadedScene());
            statusMsg = "Loaded defaults.";
            statusFrames = 120;
        }

        // Collapsible section for Reset / Save as Default
        if (ImGui::TreeNode("Reset / Save as Default"))
        {
            if (ImGui::Button("Save as Default"))
            {
                app.m_sceneConfig.SaveAsDefault(
                    app.m_loadedSceneIndex, app, app.m_engine, app.LoadedScene());
                statusMsg = "Saved as default.";
                statusFrames = 120;
            }

            if (ImGui::Button("Reset Current Scene"))
            {
                ImGui::OpenPopup("Reset Current##confirm");
            }
            ImGui::SameLine();
            if (ImGui::Button("Reset All Scenes"))
            {
                ImGui::OpenPopup("Reset All##confirm");
            }

            // Confirmation: Reset Current Scene
            if (ImGui::BeginPopupModal("Reset Current##confirm", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            {
                ImGui::Text("Reset current scene configuration to defaults?\n\nThis cannot be undone.\n\n");
                if (ImGui::Button("OK", ImVec2(120, 0)))
                {
                    app.m_sceneConfig.ResetCurrentScene(
                        app.m_loadedSceneIndex, app, app.m_engine, app.LoadedScene());
                    statusMsg = "Reset current scene.";
                    statusFrames = 120;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel", ImVec2(120, 0)))
                {
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }

            // Confirmation: Reset All Scenes
            if (ImGui::BeginPopupModal("Reset All##confirm", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            {
                ImGui::Text("Reset all scene configurations to defaults?\n\nThis cannot be undone.\n\n");
                if (ImGui::Button("OK", ImVec2(120, 0)))
                {
                    app.m_sceneConfig.ResetAllScenes(app, app.m_engine, app.LoadedScene());
                    statusMsg = "Reset all scenes.";
                    statusFrames = 120;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel", ImVec2(120, 0)))
                {
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }

            ImGui::TreePop();
        }

        // Status text (auto-clears)
        if (statusFrames > 0)
        {
            --statusFrames;
            ImGui::Text(">> %s", statusMsg);
        }
    }

    if (ImGui::CollapsingHeader("WorkMeter"))
    {
        ImGui::Text("CPU Frame: %.2f ms (%.1f FPS)", context.cpuFrameTime, 1000.0f / context.cpuFrameTime);

        const auto& gpuCheckPoints = context.gpuCheckPoints;
        const size_t gpuCheckPointCount = gpuCheckPoints.size();
        if (gpuCheckPointCount >= 2)
        {
            for (int i = 1; i < static_cast<int>(gpuCheckPointCount); i++)
            {
                const auto& checkPoint = gpuCheckPoints[i];
                if (i < static_cast<int>(gpuCheckPointCount) - 1)
                {
                    const float timeFromPrevious = checkPoint.timeStamp - gpuCheckPoints[i - 1].timeStamp;
                    ImGui::Text("GPU[%d] %s: %f ms", i, checkPoint.name.c_str(), timeFromPrevious);
                }
                else
                {
                    ImGui::Text("GPU[%d] Total: %f ms", i, checkPoint.timeStamp);
                }
            }
        }
    }

    ImGui::End();

    RtPbrSurveyEngine::LightingParams lightingParams = app.m_lightingParams;
    if (!app.m_iblEnabled)
    {
        lightingParams.iblIntensity = 0.0f;
    }
    app.m_engine.SetLightingParams(lightingParams);
    app.m_engine.SetRenderingPath(app.m_renderingPath);
    app.m_engine.SetLightingPassDebugGradient(app.m_lightingPassDebugGradient);
    app.m_engine.SetBackBufferClearColor(app.m_backBufferClearColor);
    app.m_engine.SetDisplayInstanceCount(loadedScene.DisplayInstanceCount());
    app.m_engine.SetToneMapParams(app.m_toneMapParams);
    app.m_engine.SetRenderViewMode(app.m_renderViewMode);
    app.m_engine.SetRequestHdrDump(app.m_requestHdrDump);
    app.m_requestHdrDump = false;
}

} // namespace App
