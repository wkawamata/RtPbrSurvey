#include "stdafx.h"

#include "Engine/RtPbrSurveyEngine.h"

#include <iostream>

namespace
{
using ResetReason = RtPbrSurveyEngine::PathTracingResetReason;

bool Check(bool passed, const char* label)
{
    if (!passed)
    {
        std::cerr << "FAILED: " << label << '\n';
    }
    return passed;
}

bool CheckSettings(RtPbrSurveyEngine& engine,
                   const RtPbrSurveyEngine::PathTracingSettings& settings,
                   bool reset,
                   const char* label)
{
    engine.ResetPathTracingAccumulation();
    engine.SetPathTracingSettings(settings);
    return Check(engine.GetPathTracingRuntimeState().lastResetReason ==
                     (reset ? ResetReason::Settings : ResetReason::Manual), label);
}
}

int main()
{
    GraphicsDevice graphicsDevice;
    RtPbrSurveyEngine engine(graphicsDevice);
    auto settings = engine.GetPathTracingSettings();
    bool passed = CheckSettings(engine, settings, false, "unchanged settings");

    settings.accumulate = !settings.accumulate;
    passed &= CheckSettings(engine, settings, true, "accumulation mode");
    settings.maxBounces = 16;
    passed &= CheckSettings(engine, settings, true, "bounce count");
    settings.maxBounces = UINT_MAX;
    passed &= CheckSettings(engine, settings, false, "equivalent upper bounce bound");
    settings.maxBounces = 0;
    passed &= CheckSettings(engine, settings, true, "lower bounce bound");
    passed &= CheckSettings(engine, settings, false, "equivalent lower bounce bound");
    settings.randomSeed += 1;
    passed &= CheckSettings(engine, settings, true, "random seed");
    settings.directLightingEnabled = !settings.directLightingEnabled;
    passed &= CheckSettings(engine, settings, true, "direct lighting");
    settings.environmentEnabled = !settings.environmentEnabled;
    passed &= CheckSettings(engine, settings, true, "environment");
    settings.environmentSamplingMode = 7;
    passed &= CheckSettings(engine, settings, true, "environment sampling mode");
    settings.environmentSamplingMode = UINT_MAX;
    passed &= CheckSettings(engine, settings, false, "equivalent environment mode bound");
    settings.emissiveSamplingMode = 2;
    passed &= CheckSettings(engine, settings, true, "emissive sampling mode");
    settings.emissiveSamplingMode = UINT_MAX;
    passed &= CheckSettings(engine, settings, false, "equivalent emissive mode bound");
    settings.emissiveEnabled = !settings.emissiveEnabled;
    passed &= CheckSettings(engine, settings, true, "emission");
    settings.russianRouletteEnabled = !settings.russianRouletteEnabled;
    passed &= CheckSettings(engine, settings, true, "Russian roulette");
    settings.debugOutput = RtPbrSurveyEngine::PathTracingDebugOutput::Albedo;
    passed &= CheckSettings(engine, settings, true, "debug output");
    settings.samplesPerFrame = UINT_MAX;
    passed &= CheckSettings(engine, settings, false, "sample batch does not invalidate history");
    passed &= Check(engine.GetPathTracingSettings().samplesPerFrame == 16, "upper sample bound");
    settings.samplesPerFrame = 0;
    passed &= CheckSettings(engine, settings, false, "lower sample batch does not invalidate history");
    passed &= Check(engine.GetPathTracingSettings().samplesPerFrame == 1, "lower sample bound");

    engine.SetPathTracingAccumulationPaused(true);
    engine.ResetPathTracingAccumulation();
    const auto& paused = engine.GetPathTracingRuntimeState();
    passed &= Check(paused.accumulationPaused && paused.accumulatedSampleCount == 0 &&
                        paused.frameSampleIndex == 0 && !paused.historyValid &&
                        paused.lastResetReason == ResetReason::Manual, "reset while paused");
    engine.SetPathTracingAccumulationPaused(false);
    passed &= Check(!paused.accumulationPaused && paused.lastResetReason == ResetReason::Manual,
                    "resume does not reset");

    auto camera = engine.GetCamera();
    engine.SetCamera(camera);
    passed &= Check(paused.lastResetReason == ResetReason::Manual, "unchanged camera");
    camera.pos.x += 1.0f;
    engine.SetCamera(camera);
    passed &= Check(paused.lastResetReason == ResetReason::Camera, "camera reset reason");
    engine.ResetPathTracingAccumulation();
    camera.lensShiftX += 0.12f;
    engine.SetCamera(camera);
    passed &= Check(paused.lastResetReason == ResetReason::Camera, "lens shift X reset reason");
    engine.ResetPathTracingAccumulation();
    camera.lensShiftY += 0.08f;
    engine.SetCamera(camera);
    passed &= Check(paused.lastResetReason == ResetReason::Camera, "lens shift Y reset reason");
    engine.ResetPathTracingAccumulation();
    engine.SetCamera(camera);
    passed &= Check(paused.lastResetReason == ResetReason::Manual, "unchanged lens shift preserves history");
    engine.ResetPathTracingAccumulation();
    engine.SetRenderingPath(RtPbrSurveyEngine::RenderingPath::PathTracing);
    passed &= Check(paused.lastResetReason == ResetReason::RenderingPath, "rendering path reset reason");
    engine.ResetPathTracingAccumulation();
    engine.SetRenderingPath(RtPbrSurveyEngine::RenderingPath::PathTracing);
    passed &= Check(paused.lastResetReason == ResetReason::Manual, "unchanged rendering path");

    auto lighting = engine.GetLightingParams();
    engine.SetLightingParams(lighting);
    passed &= Check(paused.lastResetReason == ResetReason::Manual, "unchanged lighting");
    lighting.iblIntensity += 1.0f;
    engine.SetLightingParams(lighting);
    passed &= Check(paused.lastResetReason == ResetReason::Lighting, "lighting reset reason");

    Engine::Scene scene;
    scene.camera = camera;
    Engine::SceneMesh mesh;
    scene.mesh = &mesh;
    engine.SetScene(scene);
    passed &= Check(paused.lastResetReason == ResetReason::Scene, "scene mesh reset reason");
    engine.ResetPathTracingAccumulation();
    engine.SetScene(scene);
    passed &= Check(paused.lastResetReason == ResetReason::Manual, "unchanged scene");
    scene.camera.fov += 1.0f;
    engine.SetScene(scene);
    passed &= Check(paused.lastResetReason == ResetReason::Camera, "scene camera reset reason");
    return passed ? 0 : 1;
}
