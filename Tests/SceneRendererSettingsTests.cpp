#include "stdafx.h"

#include "Runtime/SceneRendererSettings.h"

#include <nlohmann/json.hpp>

#include <iostream>
#include <string>

namespace
{
bool Check(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "FAILED: " << message << '\n';
    }
    return condition;
}

bool TestRoundTrip()
{
    RtPbrSurvey::SceneRendererSettings source;
    source.lighting.lightDirection = {0.25f, -0.5f, 0.75f};
    source.lighting.lightColor = {0.8f, 0.6f, 0.4f};
    source.lighting.diffuseIntensity = 2.5f;
    source.shadow.normalBias = 0.023f;
    source.shadow.sampleCount = 13;
    source.temporalUpscaler.enabled = true;
    source.temporalUpscaler.qualityMode = Engine::TemporalUpscalerQualityMode::Balanced;
    source.temporalUpscaler.renderScale = 0.67f;
    source.hybridReflection.contributionMaxDistance = 37.0f;
    source.toneMap.exposure = 1.7f;
    source.specularDebugLines.lineLength = 3.5f;
    source.renderingPath = RtPbrSurveyEngine::RenderingPath::Forward;
    source.renderViewMode = RtPbrSurveyEngine::RenderViewMode::Depth;
    source.backBufferClearColor = {0.1f, 0.2f, 0.3f, 0.9f};
    source.lightingPassDebugGradient = true;

    const std::string serialized = RtPbrSurvey::SerializeSceneRendererSettings(source);
    const nlohmann::json parsedJson = nlohmann::json::parse(serialized);
    RtPbrSurvey::SceneRendererSettings restored;
    std::string error;

    bool passed = true;
    passed &= Check(parsedJson.at("schemaVersion").get<int>() == RtPbrSurvey::SceneRendererSettings::kSchemaVersion,
                    "schema version is serialized");
    passed &= Check(RtPbrSurvey::DeserializeSceneRendererSettings(serialized, restored, &error),
                    "complete settings deserialize");
    passed &= Check(error.empty(), "successful deserialize clears error");
    passed &= Check(restored.lighting.lightDirection.x == source.lighting.lightDirection.x &&
                        restored.lighting.lightDirection.y == source.lighting.lightDirection.y &&
                        restored.lighting.lightDirection.z == source.lighting.lightDirection.z,
                    "light direction round-trips");
    passed &=
        Check(restored.lighting.diffuseIntensity == source.lighting.diffuseIntensity, "light intensity round-trips");
    passed &= Check(restored.shadow.normalBias == source.shadow.normalBias &&
                        restored.shadow.sampleCount == source.shadow.sampleCount,
                    "shadow settings round-trip");
    passed &= Check(restored.temporalUpscaler.enabled == source.temporalUpscaler.enabled &&
                        restored.temporalUpscaler.qualityMode == source.temporalUpscaler.qualityMode &&
                        restored.temporalUpscaler.renderScale == source.temporalUpscaler.renderScale,
                    "temporal upscaler settings round-trip");
    passed &=
        Check(restored.hybridReflection.contributionMaxDistance == source.hybridReflection.contributionMaxDistance,
              "hybrid reflection settings round-trip");
    passed &= Check(restored.toneMap.exposure == source.toneMap.exposure, "tone mapping settings round-trip");
    passed &= Check(restored.specularDebugLines.lineLength == source.specularDebugLines.lineLength,
                    "specular debug settings round-trip");
    passed &= Check(restored.renderingPath == source.renderingPath && restored.renderViewMode == source.renderViewMode,
                    "render modes round-trip");
    passed &= Check(restored.backBufferClearColor == source.backBufferClearColor, "clear color round-trips");
    passed &= Check(restored.lightingPassDebugGradient, "debug gradient round-trips");
    return passed;
}

bool TestMissingFieldsKeepDefaults()
{
    RtPbrSurvey::SceneRendererSettings settings;
    settings.lighting.diffuseIntensity = 3.0f;
    settings.shadow.normalBias = 0.04f;
    settings.toneMap.exposure = 1.25f;

    const std::string partial = R"({"lighting":{"lightDirection":[1.0,0.0,0.0]}})";
    bool passed =
        Check(RtPbrSurvey::DeserializeSceneRendererSettings(partial, settings), "partial settings deserialize");
    passed &= Check(settings.lighting.lightDirection.x == 1.0f, "present field is restored");
    passed &= Check(settings.lighting.diffuseIntensity == 3.0f, "missing lighting field keeps default");
    passed &= Check(settings.shadow.normalBias == 0.04f, "missing group keeps default");
    passed &= Check(settings.toneMap.exposure == 1.25f, "missing tone map keeps default");
    return passed;
}

bool TestInvalidJsonIsNonDestructive()
{
    RtPbrSurvey::SceneRendererSettings settings;
    settings.lighting.diffuseIntensity = 2.75f;
    std::string error;

    bool passed =
        Check(!RtPbrSurvey::DeserializeSceneRendererSettings("{invalid", settings, &error), "invalid JSON is rejected");
    passed &= Check(!error.empty(), "invalid JSON reports an error");
    passed &= Check(settings.lighting.diffuseIntensity == 2.75f, "invalid JSON does not modify destination settings");
    return passed;
}
} // namespace

int main()
{
    const bool passed = TestRoundTrip() && TestMissingFieldsKeepDefaults() && TestInvalidJsonIsNonDestructive();
    if (passed)
    {
        std::cout << "SceneRendererSettings tests passed.\n";
        return 0;
    }

    return 1;
}
