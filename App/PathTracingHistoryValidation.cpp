#include "stdafx.h"

#include "App/RtPbrSurveyApp.h"
#include "Platform/Win32Application.h"

#include <nlohmann/json.hpp>

void RtPbrSurveyApp::RunPathTracingHistoryValidation()
{
    static constexpr const char* names[] = {
        "baseline-16", "paused-16", "resumed-32", "reset-paused-0", "reset-repeat-16",
        "batch-4-32", "non-accumulated-index-12", "non-accumulated-repeat-12", "fresh-32",
        "camera-changed-16", "camera-fresh-16", "light-changed-16", "light-fresh-16",
        "material-changed-16", "material-fresh-16", "geometry-changed-16", "geometry-fresh-16",
        "resize-changed-16", "resize-fresh-16",
    };
    static constexpr uint64_t expectedCounts[] = {
        16, 16, 32, 0, 16, 32, 3, 3, 32, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
    };
    static constexpr UINT expectedIndices[] = {
        16, 16, 32, 0, 16, 32, 12, 12, 32, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
    };
    static constexpr const char* changedReasons[] = {"Camera", "Lighting", "Material", "Scene", "Render Size"};
    static constexpr UINT stageCount = static_cast<UINT>(std::size(names));
    const auto fail = [this](const std::string& message)
    {
        fprintf(m_logFile, "[PathTracingHistoryFailure] %s\n", message.c_str());
        fflush(m_logFile);
        DestroyWindow(Win32Application::GetHwnd());
    };

    if (m_appMode != AppMode::Running || m_renderingPath != RtPbrSurveyEngine::RenderingPath::PathTracing)
    {
        fail("A running file scene with Path Tracing is required.");
        return;
    }
    if (m_pathTracingHistoryValidationCapturePending)
    {
        if (const auto result = m_sceneRenderer.ConsumeScreenshotResult())
        {
            if (!result->succeeded)
            {
                fail(result->error);
                return;
            }
            fprintf(m_logFile, "[PathTracingHistoryCapture] %s\n", names[m_pathTracingHistoryValidationStage]);
            fflush(m_logFile);
            m_pathTracingHistoryValidationCapturePending = false;
            m_pathTracingHistoryValidationEntered = false;
            ++m_pathTracingHistoryValidationStage;
            if (m_pathTracingHistoryValidationStage == stageCount)
            {
                fprintf(m_logFile, "[PathTracingHistoryComplete] captures=%u\n", stageCount);
                fflush(m_logFile);
                DestroyWindow(Win32Application::GetHwnd());
                return;
            }
        }
    }

    const UINT stage = m_pathTracingHistoryValidationStage;
    if (!m_pathTracingHistoryValidationEntered)
    {
        auto settings = m_sceneRenderer.GetPathTracingSettings();
        m_pathTracingHistoryValidationFrames = 0;
        switch (stage)
        {
            case 0:
                settings.accumulate = true;
                settings.samplesPerFrame = 1;
                settings.randomSeed = 11;
                settings.directLightingEnabled = true;
                m_sceneRenderer.SetPathTracingSettings(settings);
                m_sceneRenderer.ResetPathTracingAccumulation();
                m_sceneRenderer.SetPathTracingAccumulationPaused(false);
                break;
            case 1:
                break;
            case 2:
                m_sceneRenderer.SetPathTracingAccumulationPaused(false);
                break;
            case 3:
                m_sceneRenderer.ResetPathTracingAccumulation();
                break;
            case 4:
                m_sceneRenderer.SetPathTracingAccumulationPaused(false);
                break;
            case 5:
                settings.samplesPerFrame = 4;
                m_sceneRenderer.SetPathTracingSettings(settings);
                if (m_sceneRenderer.GetPathTracingRuntimeState().accumulatedSampleCount != 16)
                {
                    fail("Changing sample batch size cleared history.");
                    return;
                }
                m_sceneRenderer.SetPathTracingAccumulationPaused(false);
                break;
            case 6:
                settings.accumulate = false;
                settings.samplesPerFrame = 3;
                m_sceneRenderer.SetPathTracingSettings(settings);
                m_sceneRenderer.SetPathTracingAccumulationPaused(false);
                break;
            case 7:
                m_sceneRenderer.ResetPathTracingAccumulation();
                m_sceneRenderer.SetPathTracingAccumulationPaused(false);
                break;
            case 8:
                settings.accumulate = true;
                settings.samplesPerFrame = 1;
                m_sceneRenderer.SetPathTracingSettings(settings);
                m_sceneRenderer.ResetPathTracingAccumulation();
                m_sceneRenderer.SetPathTracingAccumulationPaused(false);
                break;
            case 9:
                LoadedScene().GetScene().camera.lensShiftX += 0.12f;
                m_sceneRenderer.SetCamera(LoadedScene().GetScene().camera);
                m_sceneRenderer.SetPathTracingAccumulationPaused(false);
                break;
            case 11:
            {
                m_lightingParams = m_sceneRenderer.GetLightingParams();
                RtPbrSurvey::DirectLight light;
                light.id = 31;
                light.direction = {0.0f, -1.0f, 0.0f};
                light.color = {0.2f, 0.5f, 1.0f};
                light.intensity = 0.7f;
                m_lightingParams.lights.push_back(light);
                m_lightingParams.primaryShadowLightId = light.id;
                m_sceneRenderer.SetLightingParams(m_lightingParams);
                m_sceneRenderer.SetPathTracingAccumulationPaused(false);
                break;
            }
            case 13:
            {
                auto& materials = LoadedScene().GetMesh().materials;
                if (materials.empty())
                {
                    fail("Material mutation requires a receiver material.");
                    return;
                }
                auto& material = materials[0];
                material.roughnessFactor = 0.25f;
                material.metallicFactor = 0.5f;
                RtPbrSurveyEngine::MaterialParams params;
                params.roughnessFactor = material.roughnessFactor;
                params.metallicFactor = material.metallicFactor;
                params.ambientOcclusionFactor = material.ambientOcclusionFactor;
                params.emissiveScale = material.emissiveScale;
                m_sceneRenderer.SetMaterialParams(0, params);
                m_sceneRenderer.SetPathTracingAccumulationPaused(false);
                break;
            }
            case 15:
            {
                auto& scene = LoadedScene().GetScene();
                if (scene.instances.empty())
                {
                    fail("Geometry mutation requires a receiver instance.");
                    return;
                }
                scene.instances[0].world._41 += 0.75f;
                scene.instances[0].prevWorld = scene.instances[0].world;
                m_sceneRenderer.SetScene(scene);
                m_sceneRenderer.SetPathTracingAccumulationPaused(false);
                break;
            }
            case 17:
                m_sceneRenderer.RequestResize(1280, 720);
                m_sceneRenderer.SetPathTracingAccumulationPaused(false);
                break;
            case 10:
            case 12:
            case 14:
            case 16:
            case 18:
                m_sceneRenderer.ResetPathTracingAccumulation();
                m_sceneRenderer.SetPathTracingAccumulationPaused(false);
                break;
        }
        if (stage >= 9 && stage != 17)
        {
            const auto& reset = m_sceneRenderer.GetPathTracingRuntimeState();
            if (reset.accumulatedSampleCount != 0 || reset.frameSampleIndex != 0 || reset.historyValid)
            {
                fail("Changed state did not invalidate CPU history.");
                return;
            }
            const char* expectedReason = stage % 2 == 1 ? changedReasons[(stage - 9) / 2] : "Manual";
            if (std::string(reset.ResetReasonText()) != expectedReason)
            {
                fail("Mutation did not report the expected immediate reset reason.");
                return;
            }
        }
        m_pathTracingHistoryValidationEntryResetReason = stage == 17 ? "Pending Resize" :
            m_sceneRenderer.GetPathTracingRuntimeState().ResetReasonText();
        m_pathTracingHistoryValidationEntered = true;
    }

    if (!m_pathTracingHistoryValidationCapturePending)
    {
        const auto& state = m_sceneRenderer.GetPathTracingRuntimeState();
        const bool nonAccumulated = stage == 6 || stage == 7;
        const bool holdPaused = stage == 1 || stage == 3;
        bool ready = false;
        if (holdPaused)
        {
            if (!state.accumulationPaused || state.accumulatedSampleCount != expectedCounts[stage] ||
                state.frameSampleIndex != expectedIndices[stage])
            {
                fail("Paused count or sample index changed.");
                return;
            }
            ready = m_pathTracingHistoryValidationFrames >= 3;
        }
        else if (nonAccumulated)
        {
            ready = m_pathTracingHistoryValidationFrames == 4;
        }
        else
        {
            if (state.accumulatedSampleCount > expectedCounts[stage])
            {
                fail("Accumulated count exceeded checkpoint target.");
                return;
            }
            ready = state.accumulatedSampleCount == expectedCounts[stage] &&
                (stage < 9 || m_pathTracingHistoryValidationFrames > 0);
        }
        if (ready)
        {
            if (state.accumulatedSampleCount != expectedCounts[stage] ||
                state.frameSampleIndex != expectedIndices[stage] ||
                state.historyValid != (!nonAccumulated && stage != 3))
            {
                fail("Checkpoint count/index/validity differs from expected state.");
                return;
            }
            m_sceneRenderer.SetPathTracingAccumulationPaused(true);
            const auto context = m_sceneRenderer.GetUiFrameContext();
            if (stage >= 9)
            {
                const char* expectedReason = stage % 2 == 1 ? changedReasons[(stage - 9) / 2] : "Manual";
                const bool materialTableRefresh = stage == 13 &&
                    state.lastResetReason == RtPbrSurveyEngine::PathTracingResetReason::Scene;
                if ((!materialTableRefresh && std::string(state.ResetReasonText()) != expectedReason) ||
                    (stage >= 17 && (context.renderWidth != 1280 || context.renderHeight != 720)))
                {
                    fail("Changed-state reset reason or resized resource dimensions differ from request.");
                    return;
                }
            }
            const auto path = std::filesystem::absolute(
                m_commandLineOptions.pathTracingHistoryValidationDirectory /
                (std::string(names[stage]) + ".ptbuf"));
            const nlohmann::json record = {
                {"case", names[stage]}, {"path", path.string()},
                {"accumulatedSamples", state.accumulatedSampleCount},
                {"frameSampleIndex", state.frameSampleIndex}, {"historyValid", state.historyValid},
                {"paused", state.accumulationPaused}, {"resetReason", state.ResetReasonText()},
                {"entryResetReason", m_pathTracingHistoryValidationEntryResetReason},
                {"renderWidth", context.renderWidth}, {"renderHeight", context.renderHeight},
                {"randomSeed", m_sceneRenderer.GetPathTracingSettings().randomSeed},
                {"samplesPerFrame", m_sceneRenderer.GetPathTracingSettings().samplesPerFrame},
            };
            fprintf(m_logFile, "[PathTracingHistory] %s\n", record.dump().c_str());
            fflush(m_logFile);
            RtPbrSurvey::ScreenshotRequest request = {path};
            request.debugResourceName = "PathTracing.Accumulation";
            m_sceneRenderer.RequestScreenshot(std::move(request));
            m_pathTracingHistoryValidationCapturePending = true;
        }
    }

    UpdateUiFrame();
    m_sceneRenderer.RunFrame(
        [this](ID3D12GraphicsCommandList* commandList) { m_imguiSystem.Render(commandList); });
    ++m_pathTracingHistoryValidationFrames;
    ++m_automationFrameCounter;
    if (m_d3d12InfoQueue)
    {
        FlushD3D12DebugMessages();
    }
    if (m_pathTracingHistoryValidationFrames > 256)
    {
        fail("Checkpoint did not complete within 256 frames.");
    }
}
