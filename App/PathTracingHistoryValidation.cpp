#include "stdafx.h"

#include "App/RtPbrSurveyApp.h"
#include "Platform/Win32Application.h"

#include <nlohmann/json.hpp>

void RtPbrSurveyApp::RunPathTracingRrLifecycleValidation()
{
    using Backend = Engine::PathTracingReconstructionBackend;
    const auto fail = [this](const std::string& reason)
    {
        if (m_logFile)
        {
            fprintf(m_logFile, "[PathTracingRrLifecycleFailure] stage=%u %s\n", m_rrLifecycleStage, reason.c_str());
            fflush(m_logFile);
        }
        m_exitCode = 1;
        DestroyWindow(Win32Application::GetHwnd());
    };
    if (!m_logFile || m_appMode != AppMode::Running || m_renderingPath != RtPbrSurveyEngine::RenderingPath::PathTracing)
    {
        fail("Running PT scene and log are required");
        return;
    }
    const auto context = m_sceneRenderer.GetUiFrameContext();
    const auto& rr = context.pathTracingDiagnostics;
    const uint64_t calls = context.rayReconstructionDiagnostics.nativeEvaluateCallCount;
    const bool ready = rr.rrLivePhase == "retained-output-ready" && rr.rrRetainedOutputVerified &&
                       rr.reconstructionPresentation.output == Backend::Rr;
    const auto select = [this](Backend backend)
    {
        auto settings = m_sceneRenderer.GetPathTracingSettings();
        settings.reconstructionBackend = backend;
        settings.nrdPreview = backend == Backend::Nrd;
        m_sceneRenderer.SetPathTracingSettings(settings);
    };
    const auto invalidated = [this]()
    {
        const auto current = m_sceneRenderer.GetUiFrameContext().pathTracingDiagnostics;
        return current.reconstructionPresentation.output == Backend::Native && !current.rrRetainedOutputVerified;
    };
    const auto checkpoint = [&](const char* name)
    {
        const nlohmann::json record = {
            {"case", name},
            {"nativeCalls", calls},
            {"completed", rr.rrCompletedCount},
            {"continuous", rr.rrContinuousCount},
            {"resets", rr.rrResetCount},
            {"cancelled", rr.rrCancelledCount},
            {"inputRejected", rr.rrInputRejectionCount},
            {"publishedFrameId", rr.rrRetainedFrameId},
            {"inputFrameId", rr.rrInputFrameId},
            {"lastReset", rr.rrLastReset},
            {"phase", rr.rrLivePhase},
            {"verified", rr.rrRetainedOutputVerified},
            {"output", Engine::PathTracingReconstructionBackendName(rr.reconstructionPresentation.output)},
            {"width", context.renderWidth},
            {"height", context.renderHeight}};
        fprintf(m_logFile, "[PathTracingRrLifecycle] %s\n", record.dump().c_str());
        fflush(m_logFile);
        m_rrLifecycleCalls = calls;
        m_rrLifecycleResets = rr.rrResetCount;
        m_rrLifecyclePublished = rr.rrRetainedFrameId;
        m_rrLifecycleCancelled = rr.rrCancelledCount;
        m_rrLifecycleRejected = rr.rrInputRejectionCount;
        ++m_rrLifecycleStage;
        m_rrLifecycleFrames = 0;
        m_rrLifecycleEntered = false;
    };
    switch (m_rrLifecycleStage)
    {
        case 0:
            if (!m_rrLifecycleEntered)
            {
                m_rrLifecycleCamera = m_sceneRenderer.GetCamera();
                m_rrLifecycleEntered = true;
            }
            if (ready && rr.rrCompletedCount >= 3 && rr.rrContinuousCount >= 2)
            {
                if (rr.rrResetCount != 1)
                {
                    fail("Continuous static frames unexpectedly reset");
                    return;
                }
                checkpoint("continuous");
                m_sceneRenderer.SetPathTracingAccumulationPaused(true);
            }
            break;
        case 1:
            if (!rr.rrRetainedOutputVerified || calls != m_rrLifecycleCalls ||
                rr.rrRetainedFrameId != m_rrLifecyclePublished)
            {
                fail("Paused RR output changed or SDK re-evaluated");
                return;
            }
            if (m_rrLifecycleFrames >= 8)
            {
                checkpoint("paused");
                m_sceneRenderer.SetPathTracingAccumulationPaused(false);
            }
            break;
        case 2:
            if (ready && calls >= m_rrLifecycleCalls + 2)
            {
                if (rr.rrResetCount != m_rrLifecycleResets || rr.rrRetainedFrameId <= m_rrLifecyclePublished)
                {
                    fail("Resume lost history or reused stale output");
                    return;
                }
                checkpoint("resumed");
                select(Backend::Native);
                if (!invalidated())
                {
                    fail("Native switch retained RR presentation");
                    return;
                }
            }
            break;
        case 3:
            if (rr.reconstructionPresentation.output != Backend::Native || calls != m_rrLifecycleCalls)
            {
                fail("Native selection executed/presented RR");
                return;
            }
            if (m_rrLifecycleFrames >= 3)
            {
                checkpoint("native");
                select(Backend::Nrd);
            }
            break;
        case 4:
            if (!rr.nrdPreviewBuilt)
            {
                fail("Lifecycle validation requires both NRD and RR SDK builds");
                return;
            }
            if (calls != m_rrLifecycleCalls)
            {
                fail("NRD selection executed RR");
                return;
            }
            if (m_rrLifecycleFrames >= 3 && rr.nrdPresentationStatus == Engine::NrdRelaxPresentationStatus::Relax)
            {
                checkpoint("nrd");
                select(Backend::Rr);
                if (!invalidated())
                {
                    fail("RR switch reused pre-switch output");
                    return;
                }
            }
            break;
        case 5:
            if (ready && rr.rrResetCount > m_rrLifecycleResets)
                checkpoint("rr-restored");
            break;
        case 6:
            if (!m_rrLifecycleEntered && rr.rrLivePhase == "input-gpu-pending")
            {
                m_rrLifecycleCalls = calls;
                m_rrLifecycleResets = rr.rrResetCount;
                m_rrLifecycleCancelled = rr.rrCancelledCount;
                m_sceneRenderer.ResetPathTracingAccumulation();
                m_rrLifecycleEntered = true;
                if (!invalidated())
                {
                    fail("Pending reset presented stale output");
                    return;
                }
            }
            else if (m_rrLifecycleEntered && ready && rr.rrCancelledCount > m_rrLifecycleCancelled)
            {
                if (calls != m_rrLifecycleCalls + 1 || rr.rrResetCount <= m_rrLifecycleResets)
                {
                    fail("Cancelled input reached SDK or replacement did not reset");
                    return;
                }
                checkpoint("pending-reset");
            }
            break;
        case 7:
        case 9:
            if (rr.rrResetCount > m_rrLifecycleResets + rr.rrInputRejectionCount - m_rrLifecycleRejected ||
                rr.rrCancelledCount != m_rrLifecycleCancelled)
            {
                fail("Camera pose change reset or cancelled continuous RR");
                return;
            }
            if (ready && calls >= m_rrLifecycleCalls + 3 && !rr.rrLastReset)
            {
                checkpoint(m_rrLifecycleStage == 7 ? "camera-translation" : "camera-rotation");
                m_sceneRenderer.SetPathTracingAccumulationPaused(true);
                m_rrLifecycleCapture = 0;
            }
            else
            {
                auto camera = m_rrLifecycleCamera;
                if (m_rrLifecycleStage == 7)
                {
                    camera.pos.x += .0015f * float(m_rrLifecycleFrames + 1);
                    camera.gazePoint.x += .0015f * float(m_rrLifecycleFrames + 1);
                }
                else
                    camera.gazePoint.x += .001f * float(m_rrLifecycleFrames + 1);
                LoadedScene().GetScene().camera = camera;
                m_sceneRenderer.SetCamera(camera);
            }
            break;
        case 8:
        case 10:
        {
            // Pausing at Ready pins one production for all correlated raw
            // captures. The camera may have advanced during its GPU latency;
            // rrRetainedInput records the actual immutable camera transforms.
            static constexpr const char* resources[] = {"PathTracing.RrMotion",
                                                        "PathTracing.DenoiserPrimaryPosition",
                                                        "PathTracing.CurrentFrameRadiance",
                                                        "PathTracing.RrColor",
                                                        "PathTracing.RrOutputAlternate",
                                                        "PathTracing.RrOutput"};
            if (!ready || calls != m_rrLifecycleCalls || rr.rrRetainedFrameId != m_rrLifecyclePublished)
            {
                fail("Motion capture did not retain one verified production");
                return;
            }
            if (!m_rrLifecycleEntered)
            {
                const std::string prefix = m_rrLifecycleStage == 8 ? "translation-" : "rotation-";
                RtPbrSurvey::ScreenshotRequest request;
                request.path =
                    std::filesystem::absolute(m_commandLineOptions.pathTracingRrLifecycleValidationDirectory /
                                              (prefix + std::to_string(m_rrLifecycleCapture) + ".ptbuf"));
                request.debugResourceName = resources[m_rrLifecycleCapture];
                // Capture both slots; raw metadata identifies the published one.
                m_sceneRenderer.RequestScreenshot(request);
                m_rrLifecycleEntered = true;
            }
            else if (const auto result = m_sceneRenderer.ConsumeScreenshotResult())
            {
                if (!result->succeeded)
                {
                    fail(result->error);
                    return;
                }
                ++m_rrLifecycleCapture;
                m_rrLifecycleEntered = false;
                if (m_rrLifecycleCapture == std::size(resources))
                {
                    ++m_rrLifecycleStage;
                    m_rrLifecycleFrames = 0;
                    m_rrLifecycleEntered = false;
                    m_rrLifecycleCamera = m_sceneRenderer.GetCamera();
                    m_sceneRenderer.SetPathTracingAccumulationPaused(false);
                }
            }
            break;
        }
        case 11:
            if (ready && calls >= m_rrLifecycleCalls + 2 && !rr.rrLastReset)
            {
                if (rr.rrResetCount > m_rrLifecycleResets + rr.rrInputRejectionCount - m_rrLifecycleRejected ||
                    rr.rrCancelledCount != m_rrLifecycleCancelled)
                {
                    fail("Stopping camera lost continuous RR history");
                    return;
                }
                checkpoint("camera-stopped");
            }
            break;
        case 12:
            if (!m_rrLifecycleEntered)
            {
                m_rrLifecycleCamera = m_sceneRenderer.GetCamera();
                auto camera = m_rrLifecycleCamera;
                camera.gazePoint = {camera.pos.x, camera.pos.y, camera.pos.z - 1};
                LoadedScene().GetScene().camera = camera;
                m_sceneRenderer.SetCamera(camera);
                m_rrLifecycleEntered = true;
            }
            else if (rr.rrInputRejectionCount > m_rrLifecycleRejected &&
                     rr.reconstructionPresentation.output == Backend::Native && !rr.rrRetainedOutputVerified)
                checkpoint("input-rejected");
            break;
        case 13:
            if (!m_rrLifecycleEntered)
            {
                LoadedScene().GetScene().camera = m_rrLifecycleCamera;
                m_sceneRenderer.SetCamera(m_rrLifecycleCamera);
                m_rrLifecycleEntered = true;
            }
            else if (ready && calls >= m_rrLifecycleCalls + 2 && !rr.rrLastReset)
            {
                if (rr.rrResetCount <= m_rrLifecycleResets)
                {
                    fail("Input rejection did not reset SDK history before recovery");
                    return;
                }
                checkpoint("input-recovered");
            }
            break;
        case 14:
            if (!m_rrLifecycleEntered)
            {
                auto camera = m_sceneRenderer.GetCamera();
                camera.fov += 2;
                LoadedScene().GetScene().camera = camera;
                m_sceneRenderer.SetCamera(camera);
                m_rrLifecycleEntered = true;
                if (!invalidated())
                {
                    fail("Projection change exposed stale RR output");
                    return;
                }
            }
            else if (ready && rr.rrResetCount > m_rrLifecycleResets)
                checkpoint("projection-changed");
            break;
        case 15:
        case 16:
        {
            const UINT width = m_rrLifecycleStage == 15 ? 320 : 256;
            const UINT height = m_rrLifecycleStage == 15 ? 180 : 144;
            if (!m_rrLifecycleEntered)
            {
                m_sceneRenderer.RequestResize(width, height);
                m_rrLifecycleEntered = true;
            }
            else if (ready && context.renderWidth == width && context.renderHeight == height &&
                     rr.rrResetCount > m_rrLifecycleResets)
                checkpoint(m_rrLifecycleStage == 15 ? "resized" : "resize-restored");
            break;
        }
        case 17:
            if (!m_rrLifecycleEntered)
            {
                m_sceneRenderer.SetPathTracingAccumulationPaused(true);
                const auto path = std::filesystem::absolute(
                    m_commandLineOptions.pathTracingRrLifecycleValidationDirectory / "viewport.png");
                m_sceneRenderer.RequestScreenshot(RtPbrSurvey::ScreenshotRequest{path});
                m_rrLifecycleEntered = true;
            }
            else if (const auto result = m_sceneRenderer.ConsumeScreenshotResult())
            {
                if (!result->succeeded)
                {
                    fail(result->error);
                    return;
                }
                fprintf(m_logFile, "[PathTracingRrLifecycleComplete] checkpoints=15\n");
                fflush(m_logFile);
                DestroyWindow(Win32Application::GetHwnd());
                return;
            }
            break;
    }
    UpdateUiFrame();
    m_sceneRenderer.RunFrame([this](ID3D12GraphicsCommandList* commands) { m_imguiSystem.Render(commands); });
    ++m_rrLifecycleFrames;
    ++m_automationFrameCounter;
    if (m_d3d12InfoQueue)
        FlushD3D12DebugMessages();
    if (m_rrLifecycleFrames > 512)
        fail("Lifecycle checkpoint timed out: phase=" + rr.rrLivePhase + " failure=" + rr.rrLiveFailure +
             " calls=" + std::to_string(calls) + " completed=" + std::to_string(rr.rrCompletedCount));
}

void RtPbrSurveyApp::RunPathTracingHistoryValidation()
{
    if (m_logFile == nullptr)
    {
        OutputDebugStringA("[PathTracingHistoryFailure] -LogToFile could not be opened.\n");
        m_exitCode = 1;
        DestroyWindow(Win32Application::GetHwnd());
        return;
    }
    static constexpr const char* names[] = {
        "baseline-16",
        "paused-16",
        "resumed-32",
        "reset-paused-0",
        "reset-repeat-16",
        "batch-4-32",
        "non-accumulated-index-12",
        "non-accumulated-repeat-12",
        "fresh-32",
        "camera-changed-16",
        "camera-fresh-16",
        "light-changed-16",
        "light-fresh-16",
        "material-changed-16",
        "material-fresh-16",
        "geometry-changed-16",
        "geometry-fresh-16",
        "resize-changed-16",
        "resize-fresh-16",
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
        m_exitCode = 1;
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
                if (settings.nrdPreview)
                {
                    const auto expectedPreview =
                        m_sceneRenderer.GetUiFrameContext().pathTracingDiagnostics.nrdFailureReason.empty()
                            ? Engine::NrdRelaxPresentationStatus::Relax
                            : Engine::NrdRelaxPresentationStatus::BackendUnavailable;
                    const auto before = m_sceneRenderer.GetPathTracingRuntimeState();
                    settings.nrdPreview = false;
                    m_sceneRenderer.SetPathTracingSettings(settings);
                    const bool native =
                        m_sceneRenderer.GetUiFrameContext().pathTracingDiagnostics.nrdPresentationStatus ==
                        Engine::NrdRelaxPresentationStatus::Native;
                    settings.nrdPreview = true;
                    m_sceneRenderer.SetPathTracingSettings(settings);
                    const auto after = m_sceneRenderer.GetPathTracingRuntimeState();
                    const bool previewMatches =
                        m_sceneRenderer.GetUiFrameContext().pathTracingDiagnostics.nrdPresentationStatus ==
                        expectedPreview;
                    if (!native || !previewMatches || before.accumulatedSampleCount != after.accumulatedSampleCount ||
                        before.frameSampleIndex != after.frameSampleIndex || before.historyValid != after.historyValid)
                    {
                        fail("Presentation-only switch changed PT history or discarded current NRD output.");
                        return;
                    }
                }
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
                scene.instances[0].world._14 += 0.75f;
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
        m_pathTracingHistoryValidationEntryResetReason =
            stage == 17 ? "Pending Resize" : m_sceneRenderer.GetPathTracingRuntimeState().ResetReasonText();
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
                const bool materialTableRefresh =
                    stage == 13 && state.lastResetReason == RtPbrSurveyEngine::PathTracingResetReason::Scene;
                if ((!materialTableRefresh && std::string(state.ResetReasonText()) != expectedReason) ||
                    (stage >= 17 && (context.renderWidth != 1280 || context.renderHeight != 720)))
                {
                    fail("Changed-state reset reason or resized resource dimensions differ from request.");
                    return;
                }
            }
            const auto path = std::filesystem::absolute(m_commandLineOptions.pathTracingHistoryValidationDirectory /
                                                        (std::string(names[stage]) + ".ptbuf"));
            const nlohmann::json record = {
                {"case", names[stage]},
                {"path", path.string()},
                {"resource",
                 m_commandLineOptions.debugPreviewResourceName.empty() ? "PathTracing.Accumulation"
                                                                       : m_commandLineOptions.debugPreviewResourceName},
                {"accumulatedSamples", state.accumulatedSampleCount},
                {"frameSampleIndex", state.frameSampleIndex},
                {"historyValid", state.historyValid},
                {"paused", state.accumulationPaused},
                {"resetReason", state.ResetReasonText()},
                {"entryResetReason", m_pathTracingHistoryValidationEntryResetReason},
                {"renderWidth", context.renderWidth},
                {"renderHeight", context.renderHeight},
                {"randomSeed", m_sceneRenderer.GetPathTracingSettings().randomSeed},
                {"samplesPerFrame", m_sceneRenderer.GetPathTracingSettings().samplesPerFrame},
            };
            fprintf(m_logFile, "[PathTracingHistory] %s\n", record.dump().c_str());
            fflush(m_logFile);
            RtPbrSurvey::ScreenshotRequest request = {path};
            request.debugResourceName = m_commandLineOptions.debugPreviewResourceName.empty()
                                            ? "PathTracing.Accumulation"
                                            : m_commandLineOptions.debugPreviewResourceName;
            m_sceneRenderer.RequestScreenshot(std::move(request));
            m_pathTracingHistoryValidationCapturePending = true;
        }
    }

    UpdateUiFrame();
    m_sceneRenderer.RunFrame([this](ID3D12GraphicsCommandList* commandList) { m_imguiSystem.Render(commandList); });
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
