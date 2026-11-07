#include "stdafx.h"

#include "Runtime/SceneRendererSettings.h"

#include <nlohmann/json.hpp>

#include <iostream>
#include <limits>
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

bool TestLightingSwitchRoundTrip()
{
    bool passed = true;
    for (bool enabled : {false, true})
    {
        RtPbrSurvey::SceneRendererSettings source;
        source.lighting.diffuseIblEnabled = enabled;
        source.lighting.specularIblEnabled = !enabled;
        source.lighting.primaryShadowLightId = enabled ? source.lighting.lights[0].id : 0;
        source.shadow.enabled = enabled;
        RtPbrSurvey::SceneRendererSettings restored;
        const std::string serialized = RtPbrSurvey::SerializeSceneRendererSettings(source);
        passed &= Check(RtPbrSurvey::DeserializeSceneRendererSettings(serialized, restored),
                        "lighting switches deserialize");
        passed &= Check(restored.lighting.diffuseIblEnabled == enabled &&
                            restored.lighting.specularIblEnabled == !enabled &&
                            restored.lighting.primaryShadowLightId == source.lighting.primaryShadowLightId &&
                            restored.shadow.enabled == enabled,
                        "IBL and primary directional shadow switches round-trip");
    }
    return passed;
}

bool TestRoundTrip()
{
    RtPbrSurvey::SceneRendererSettings source;
    source.lighting.lights[0].direction = {1.0f, 0.0f, 0.0f};
    source.lighting.lights[0].color = {0.8f, 0.6f, 0.4f};
    source.lighting.lights[0].intensity = 2.5f;
    source.shadow.normalBias = 0.023f;
    source.shadow.sampleCount = 13;
    source.diffuseEnvironmentVisibility = {true, 0.6f, 32, 0.02f, 0.005f, 250.0f};
    source.diffuseEnvironmentVisibility.sampleRotationEnabled = false;
    source.diffuseEnvironmentVisibility.forceMaskedGeometry = true;
    source.diffuseEnvironmentVisibility.spatialFilterEnabled = false;
    source.diffuseEnvironmentVisibility.filterPlaneDistance = 0.02f;
    source.diffuseEnvironmentVisibility.filterWorldRadius = 0.3f;
    source.temporalUpscaler.enabled = true;
    source.temporalUpscaler.qualityMode = Engine::TemporalUpscalerQualityMode::Balanced;
    source.temporalUpscaler.renderScale = 0.67f;
    source.hybridReflection.contributionMaxDistance = 37.0f;
    source.pathTracing.accumulate = false;
    source.pathTracing.samplesPerFrame = 4;
    source.pathTracing.pixelCenterPrimary = true;
    source.pathTracing.denoiserDistanceDiagnostics = true;
    source.pathTracing.nrdPreview = true;
    source.pathTracing.maxBounces = 7;
    source.pathTracing.randomSeed = 42;
    source.pathTracing.directLightingEnabled = false;
    source.pathTracing.environmentEnabled = false;
    source.pathTracing.environmentSamplingMode = 7;
    source.pathTracing.emissiveSamplingMode = 2;
    source.pathTracing.emissiveEnabled = false;
    source.pathTracing.russianRouletteEnabled = true;
    source.pathTracing.debugOutput = RtPbrSurveyEngine::PathTracingDebugOutput::WorldNormal;
    source.toneMap.exposure = 1.7f;
    source.specularDebugLines.lineLength = 3.5f;
    source.renderingPath = RtPbrSurveyEngine::RenderingPath::PathTracing;
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
    const auto& visibility = restored.diffuseEnvironmentVisibility;
    passed &= Check(visibility.forceMaskedGeometry, "forced MASK visibility round-trips");
    nlohmann::json legacyJson = parsedJson;
    legacyJson["diffuseEnvironmentVisibility"].erase("forceMaskedGeometry");
    RtPbrSurvey::SceneRendererSettings legacy;
    legacy.diffuseEnvironmentVisibility.forceMaskedGeometry = true;
    passed &= Check(RtPbrSurvey::DeserializeSceneRendererSettings(legacyJson.dump(), legacy, &error) &&
                        !legacy.diffuseEnvironmentVisibility.forceMaskedGeometry,
                    "legacy visibility settings default forced MASK to off");
    passed &= Check(visibility.enabled && visibility.strength == 0.6f && visibility.sampleCount == 32 &&
                        visibility.normalBias == 0.02f && visibility.rayTMin == 0.005f && visibility.traceDistance == 250,
                    "diffuse environment visibility settings round-trip");
    passed &= Check(!visibility.sampleRotationEnabled && !visibility.spatialFilterEnabled &&
                        visibility.filterPlaneDistance == 0.02f && visibility.filterWorldRadius == 0.3f,
                    "visibility sampling and filter settings round-trip");
    passed &= Check(restored.lighting.lights[0].direction.x == source.lighting.lights[0].direction.x &&
                        restored.lighting.lights[0].direction.y == source.lighting.lights[0].direction.y &&
                        restored.lighting.lights[0].direction.z == source.lighting.lights[0].direction.z,
                    "light direction round-trips");
    passed &= Check(restored.lighting.lights[0].intensity == source.lighting.lights[0].intensity,
                    "light intensity round-trips");
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
    passed &=
        Check(restored.pathTracing.accumulate == source.pathTracing.accumulate &&
                  restored.pathTracing.samplesPerFrame == source.pathTracing.samplesPerFrame &&
                  restored.pathTracing.pixelCenterPrimary == source.pathTracing.pixelCenterPrimary &&
                  restored.pathTracing.denoiserDistanceDiagnostics == source.pathTracing.denoiserDistanceDiagnostics &&
                  restored.pathTracing.nrdPreview == source.pathTracing.nrdPreview &&
                  restored.pathTracing.maxBounces == source.pathTracing.maxBounces &&
                  restored.pathTracing.randomSeed == source.pathTracing.randomSeed &&
                  restored.pathTracing.directLightingEnabled == source.pathTracing.directLightingEnabled &&
                  restored.pathTracing.environmentEnabled == source.pathTracing.environmentEnabled &&
                  restored.pathTracing.environmentSamplingMode == source.pathTracing.environmentSamplingMode &&
                  restored.pathTracing.emissiveSamplingMode == source.pathTracing.emissiveSamplingMode &&
                  restored.pathTracing.emissiveEnabled == source.pathTracing.emissiveEnabled &&
                  restored.pathTracing.russianRouletteEnabled == source.pathTracing.russianRouletteEnabled &&
                  restored.pathTracing.debugOutput == source.pathTracing.debugOutput,
              "path tracing settings round-trip");
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
    settings.lighting.lights[0].intensity = 3.0f;
    settings.shadow.normalBias = 0.04f;
    settings.pathTracing.maxBounces = 9;
    settings.toneMap.exposure = 1.25f;

    const std::string partial = R"({"lighting":{"lightDirection":[1.0,0.0,0.0]}})";
    bool passed =
        Check(RtPbrSurvey::DeserializeSceneRendererSettings(partial, settings), "partial settings deserialize");
    passed &= Check(settings.lighting.lights[0].direction.x == -1.0f, "present field is restored");
    passed &= Check(settings.lighting.lights[0].intensity == 3.0f, "missing legacy intensity keeps current default");
    passed &= Check(settings.shadow.normalBias == 0.04f, "missing group keeps default");
    passed &= Check(settings.pathTracing.maxBounces == 9, "missing path tracing group keeps default");
    passed &= Check(settings.pathTracing.debugOutput == RtPbrSurveyEngine::PathTracingDebugOutput::Radiance,
                    "missing path tracing output keeps the radiance default");
    passed &= Check(settings.toneMap.exposure == 1.25f, "missing tone map keeps default");
    return passed;
}

bool TestInvalidJsonIsNonDestructive()
{
    RtPbrSurvey::SceneRendererSettings settings;
    settings.lighting.lights[0].intensity = 2.75f;
    std::string error;

    bool passed =
        Check(!RtPbrSurvey::DeserializeSceneRendererSettings("{invalid", settings, &error), "invalid JSON is rejected");
    passed &= Check(!error.empty(), "invalid JSON reports an error");
    passed &=
        Check(settings.lighting.lights[0].intensity == 2.75f, "invalid JSON does not modify destination settings");
    return passed;
}

bool TestMultipleLightsAndInvalidInputs()
{
    RtPbrSurvey::SceneRendererSettings settings;
    for (uint32_t id = 2; id <= 16; ++id)
    {
        RtPbrSurvey::DirectLight light;
        light.id = id;
        light.name = "Test " + std::to_string(id);
        light.type = static_cast<RtPbrSurvey::LightType>(id % 3);
        light.enabled = id % 2 == 0;
        settings.lighting.lights.push_back(light);
    }
    const nlohmann::json valid = RtPbrSurvey::SceneRendererSettingsToJson(settings);
    RtPbrSurvey::SceneRendererSettings restored;
    bool passed = Check(RtPbrSurvey::SceneRendererSettingsFromJson(valid, restored, restored), "16 mixed lights load");
    passed &= Check(restored.lighting.lights == settings.lighting.lights, "all light fields round-trip");
    const auto reject = [&passed, &settings](const nlohmann::json& input)
    {
        const std::string before = RtPbrSurvey::SerializeSceneRendererSettings(settings);
        std::string error;
        passed &= Check(!RtPbrSurvey::SceneRendererSettingsFromJson(input, settings, settings, &error),
                        "invalid lights rejected");
        passed &= Check(!error.empty() && before == RtPbrSurvey::SerializeSceneRendererSettings(settings),
                        "failed light load retains active settings and reports an error");
    };
    nlohmann::json invalid = valid;
    invalid["schemaVersion"] = 999;
    reject(invalid);
    invalid = valid;
    invalid["lighting"]["diffuseIntensity"] = 1.0;
    reject(invalid);
    invalid = valid;
    invalid["lighting"]["lights"].push_back(valid["lighting"]["lights"][0]);
    reject(invalid);
    for (const nlohmann::json& field : std::vector<nlohmann::json>{{{"type", "area"}},
                                                                   {{"id", -1}},
                                                                   {{"id", 0}},
                                                                   {{"id", 2}},
                                                                   {{"id", 1.5}},
                                                                   {{"direction", {0, 0, 0}}},
                                                                   {{"direction", {0, 1}}},
                                                                   {{"color", {-1, 1, 1}}},
                                                                   {{"intensity", -1}},
                                                                   {{"range", 0}},
                                                                   {{"innerCone", 30}},
                                                                   {{"outerCone", 90}}})
    {
        invalid = valid;
        invalid["lighting"]["lights"][0].update(field);
        reject(invalid);
    }
    nlohmann::json empty = valid;
    empty["lighting"]["lights"] = nlohmann::json::array();
    passed &=
        Check(RtPbrSurvey::SceneRendererSettingsFromJson(empty, restored, restored) && restored.lighting.lights.empty(),
              "empty light array remains empty");
    passed &=
        Check(RtPbrSurvey::FindShadowLight(restored.lighting.lights, 1) == nullptr, "missing primary has no shadow");
    restored.lighting.lights = {RtPbrSurvey::DirectLight{}};
    restored.lighting.lights[0].enabled = false;
    passed &=
        Check(RtPbrSurvey::FindShadowLight(restored.lighting.lights, 1) == nullptr, "disabled primary has no shadow");
    restored.lighting.lights[0].enabled = true;
    restored.lighting.lights[0].type = RtPbrSurvey::LightType::Point;
    passed &= Check(RtPbrSurvey::FindShadowLight(restored.lighting.lights, 1) == nullptr,
                    "point primary has no directional shadow");
    const nlohmann::json legacy = {{"schemaVersion", 3},
                                   {"lighting",
                                    {{"lightDirection", {0, 2, 0}},
                                     {"lightColor", {0.5, 0.25, 1.0}},
                                     {"diffuseIntensity", 2.5},
                                     {"directLightEnabled", false}}}};
    passed &= Check(RtPbrSurvey::SceneRendererSettingsFromJson(legacy, restored, restored), "legacy light migrates");
    passed &= Check(restored.lighting.lights.size() == 1 && restored.lighting.lights[0].direction.y == -1.0f &&
                        restored.lighting.lights[0].color.x == 0.5f && restored.lighting.lights[0].intensity == 2.5f &&
                        !restored.lighting.lights[0].enabled,
                    "legacy direction, radiance and disabled state preserved");
    return passed;
}

bool TestPathTracingValuesAreBounded()
{
    RtPbrSurvey::SceneRendererSettings settings;
    const std::string invalidValues = R"({"pathTracing":{"samplesPerFrame":0,"maxBounces":99,"emissiveSamplingMode":99},"renderingPath":99})";

    bool passed =
        Check(RtPbrSurvey::DeserializeSceneRendererSettings(invalidValues, settings), "bounded settings deserialize");
    passed &= Check(settings.pathTracing.samplesPerFrame == 1, "samples per frame is clamped");
    passed &= Check(settings.pathTracing.maxBounces == 16, "max bounces is clamped");
    passed &= Check(settings.pathTracing.emissiveSamplingMode == 2, "emissive sampling mode is clamped");
    passed &= Check(settings.renderingPath == RtPbrSurveyEngine::RenderingPath::Deferred,
                    "invalid rendering path keeps default");
    return passed;
}
} // namespace

int main()
{
    const auto visibilitySettings = []()
    {
        RtPbrSurvey::SceneRendererSettings settings;
        bool valid = Check(RtPbrSurvey::DeserializeSceneRendererSettings("{}", settings) &&
                               !settings.diffuseEnvironmentVisibility.enabled,
                           "legacy settings default to visibility disabled");
        for (const char* json : {
            R"({"diffuseEnvironmentVisibility":{"sampleCount":0}})",
            R"({"diffuseEnvironmentVisibility":{"sampleCount":65}})",
            R"({"diffuseEnvironmentVisibility":{"strength":-1}})",
            R"({"diffuseEnvironmentVisibility":{"strength":1.1}})",
            R"({"diffuseEnvironmentVisibility":{"normalBias":-1}})",
            R"({"diffuseEnvironmentVisibility":{"filterPlaneDistance":0}})",
            R"({"diffuseEnvironmentVisibility":{"filterWorldRadius":-1}})",
            R"({"diffuseEnvironmentVisibility":{"rayTMin":2,"traceDistance":1}})"})
        {
            valid &= Check(!RtPbrSurvey::DeserializeSceneRendererSettings(json, settings) &&
                               !settings.diffuseEnvironmentVisibility.enabled &&
                               settings.diffuseEnvironmentVisibility.sampleCount == 16,
                           "invalid visibility settings rejected without mutation");
        }
        auto invalid = settings.diffuseEnvironmentVisibility;
        invalid.strength = std::numeric_limits<float>::quiet_NaN();
        valid &= Check(!invalid.IsValid(), "NaN visibility strength rejected");
        invalid = settings.diffuseEnvironmentVisibility;
        invalid.traceDistance = std::numeric_limits<float>::infinity();
        valid &= Check(!invalid.IsValid(), "infinite trace distance rejected");
        invalid = settings.diffuseEnvironmentVisibility;
        invalid.filterPlaneDistance = std::numeric_limits<float>::quiet_NaN();
        valid &= Check(!invalid.IsValid(), "NaN filter plane distance rejected");
        invalid = settings.diffuseEnvironmentVisibility;
        invalid.filterWorldRadius = std::numeric_limits<float>::infinity();
        valid &= Check(!invalid.IsValid(), "infinite filter world radius rejected");
        return valid;
    };
    const auto reconstructionSettings = []()
    {
        using Backend = Engine::PathTracingReconstructionBackend;
        RtPbrSurvey::SceneRendererSettings settings;
        bool valid = true;
        for (const char* name : {"native", "nrd", "rr"})
        {
            const std::string text = std::string("{\"pathTracing\":{\"reconstructionBackend\":\"") + name +
                                     "\",\"reconstructionDiagnostics\":true,\"rrContinuous\":true}}";
            valid &= Check(RtPbrSurvey::DeserializeSceneRendererSettings(text, settings), "Backend selection parses");
            const auto json = RtPbrSurvey::SceneRendererSettingsToJson(settings);
            valid &= Check(json["pathTracing"]["reconstructionBackend"] == name &&
                               settings.pathTracing.reconstructionDiagnostics &&
                               json["pathTracing"]["rrContinuous"] == true && settings.pathTracing.rrContinuous,
                           "Backend selection and diagnostics persist");
        }
        const auto previous = settings.pathTracing.reconstructionBackend;
        valid &= Check(!RtPbrSurvey::DeserializeSceneRendererSettings(
                           R"({"pathTracing":{"reconstructionBackend":"typo"}})", settings) &&
                           settings.pathTracing.reconstructionBackend == previous,
                       "Unknown backend rejected without mutation");
        settings = {};
        valid &=
            Check(RtPbrSurvey::DeserializeSceneRendererSettings(R"({"pathTracing":{"nrdPreview":true}})", settings) &&
                      Engine::EffectivePathTracingBackend(settings.pathTracing.reconstructionBackend,
                                                          settings.pathTracing.nrdPreview) == Backend::Nrd,
                  "Legacy preview still selects NRD");
        return valid;
    };
    const bool passed = TestLightingSwitchRoundTrip() && TestRoundTrip() && TestMissingFieldsKeepDefaults() && TestInvalidJsonIsNonDestructive() &&
                        TestPathTracingValuesAreBounded() && TestMultipleLightsAndInvalidInputs() &&
                        reconstructionSettings() && visibilitySettings();
    if (passed)
    {
        std::cout << "SceneRendererSettings tests passed.\n";
        return 0;
    }

    return 1;
}
