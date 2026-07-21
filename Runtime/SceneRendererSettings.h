#pragma once

#include "Engine/RtPbrSurveyEngine.h"

#include <nlohmann/json_fwd.hpp>

#include <array>
#include <string>
#include <string_view>

namespace RtPbrSurvey
{
struct SceneRendererSettings
{
    static constexpr int kSchemaVersion = 1;

    RtPbrSurveyEngine::LightingParams lighting;
    RtPbrSurveyEngine::ShadowSettings shadow;
    Engine::TemporalUpscalerSettings temporalUpscaler;
    RtPbrSurveyEngine::HybridReflectionSettings hybridReflection;
    RtPbrSurveyEngine::ToneMapParams toneMap;
    RtPbrSurveyEngine::SpecularDebugLineSettings specularDebugLines;
    RtPbrSurveyEngine::RenderingPath renderingPath = RtPbrSurveyEngine::RenderingPath::Deferred;
    RtPbrSurveyEngine::RenderViewMode renderViewMode = RtPbrSurveyEngine::RenderViewMode::LightPass;
    std::array<float, 4> backBufferClearColor = {0.0f, 0.2f, 0.4f, 1.0f};
    bool lightingPassDebugGradient = false;
};

nlohmann::json SceneRendererSettingsToJson(const SceneRendererSettings& settings);
bool SceneRendererSettingsFromJson(const nlohmann::json& json,
                                   const SceneRendererSettings& defaults,
                                   SceneRendererSettings& settings,
                                   std::string* error = nullptr);

std::string SerializeSceneRendererSettings(const SceneRendererSettings& settings, int indent = 2);
bool DeserializeSceneRendererSettings(std::string_view jsonText,
                                      SceneRendererSettings& settings,
                                      std::string* error = nullptr);
} // namespace RtPbrSurvey
