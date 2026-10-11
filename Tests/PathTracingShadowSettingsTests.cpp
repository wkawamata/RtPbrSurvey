#include "stdafx.h"

#include "Engine/RtPbrSurveyEngine.h"

#include <iostream>

namespace
{
bool CheckReset(RtPbrSurveyEngine& engine,
                const RtPbrSurveyEngine::ShadowSettings& settings,
                bool expectedReset,
                const char* label)
{
    engine.ResetPathTracingAccumulation();
    engine.SetShadowSettings(settings);
    const auto expectedReason = expectedReset ? RtPbrSurveyEngine::PathTracingResetReason::Settings
                                              : RtPbrSurveyEngine::PathTracingResetReason::Manual;
    const bool passed = engine.GetPathTracingRuntimeState().lastResetReason == expectedReason;
    if (!passed)
    {
        std::cerr << "FAILED: " << label << '\n';
    }
    return passed;
}
} // namespace

int main()
{
    GraphicsDevice graphicsDevice;
    RtPbrSurveyEngine engine(graphicsDevice);
    auto settings = engine.GetShadowSettings();

    bool passed = CheckReset(engine, settings, false, "unchanged settings");
    settings.normalBias += 0.01f;
    passed &= CheckReset(engine, settings, true, "normal bias change");
    settings.enabled = !settings.enabled;
    passed &= CheckReset(engine, settings, true, "shadow enable change");
    settings.rayTMin += 0.001f;
    passed &= CheckReset(engine, settings, true, "ray TMin change");
    settings.rayTMax -= 1.0f;
    passed &= CheckReset(engine, settings, true, "ray TMax change");
    settings.softShadowEnabled = !settings.softShadowEnabled;
    settings.sampleCount += 1;
    settings.lightAngularRadius += 0.01f;
    settings.jitterStrength += 0.1f;
    passed &= CheckReset(engine, settings, false, "unused soft shadow settings");

    return passed ? 0 : 1;
}
