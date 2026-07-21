#include "stdafx.h"

#include "Runtime/SceneRendererSettings.h"

#include <nlohmann/json.hpp>

namespace
{
using json = nlohmann::json;

json Float3ToJson(const DirectX::XMFLOAT3& value)
{
    return json{value.x, value.y, value.z};
}

json Float4ToJson(const std::array<float, 4>& value)
{
    return json{value[0], value[1], value[2], value[3]};
}

DirectX::XMFLOAT3 Float3FromJson(const json& value, const DirectX::XMFLOAT3& defaults)
{
    if (!value.is_array() || value.size() != 3)
    {
        return defaults;
    }

    return {value[0].get<float>(), value[1].get<float>(), value[2].get<float>()};
}

std::array<float, 4> Float4FromJson(const json& value, const std::array<float, 4>& defaults)
{
    if (!value.is_array() || value.size() != 4)
    {
        return defaults;
    }

    return {value[0].get<float>(), value[1].get<float>(), value[2].get<float>(), value[3].get<float>()};
}

template <typename Enum> Enum EnumValue(const json& object, const char* name, Enum defaults)
{
    return static_cast<Enum>(object.value(name, static_cast<int>(defaults)));
}
} // namespace

namespace RtPbrSurvey
{
nlohmann::json SceneRendererSettingsToJson(const SceneRendererSettings& settings)
{
    json lighting;
    lighting["lightDirection"] = Float3ToJson(settings.lighting.lightDirection);
    lighting["lightColor"] = Float3ToJson(settings.lighting.lightColor);
    lighting["iblIntensity"] = settings.lighting.iblIntensity;
    lighting["diffuseIntensity"] = settings.lighting.diffuseIntensity;
    lighting["skyboxEnabled"] = settings.lighting.skyboxEnabled;
    lighting["skyboxPreview"] = settings.lighting.skyboxPreview;
    lighting["skyboxPreviewExposure"] = settings.lighting.skyboxPreviewExposure;
    lighting["directLightEnabled"] = settings.lighting.directLightEnabled;
    lighting["diffuseIblEnabled"] = settings.lighting.diffuseIblEnabled;
    lighting["specularIblEnabled"] = settings.lighting.specularIblEnabled;
    lighting["emissiveEnabled"] = settings.lighting.emissiveEnabled;
    lighting["iblDebugMip"] = settings.lighting.iblDebugMip;
    lighting["iblDebugExposure"] = settings.lighting.iblDebugExposure;

    json shadow;
    shadow["enabled"] = settings.shadow.enabled;
    shadow["normalBias"] = settings.shadow.normalBias;
    shadow["rayTMin"] = settings.shadow.rayTMin;
    shadow["rayTMax"] = settings.shadow.rayTMax;
    shadow["softShadowEnabled"] = settings.shadow.softShadowEnabled;
    shadow["sampleCount"] = settings.shadow.sampleCount;
    shadow["lightAngularRadius"] = settings.shadow.lightAngularRadius;
    shadow["jitterStrength"] = settings.shadow.jitterStrength;

    json temporalUpscaler;
    temporalUpscaler["enabled"] = settings.temporalUpscaler.enabled;
    temporalUpscaler["backend"] = static_cast<int>(settings.temporalUpscaler.backend);
    temporalUpscaler["qualityMode"] = static_cast<int>(settings.temporalUpscaler.qualityMode);
    temporalUpscaler["renderScale"] = settings.temporalUpscaler.renderScale;
    temporalUpscaler["sharpness"] = settings.temporalUpscaler.sharpness;
    temporalUpscaler["autoExposure"] = settings.temporalUpscaler.autoExposure;

    json hybridReflection;
    hybridReflection["enabled"] = settings.hybridReflection.enabled;
    hybridReflection["materialGateEnabled"] = settings.hybridReflection.materialGateEnabled;
    hybridReflection["maxRoughness"] = settings.hybridReflection.maxRoughness;
    hybridReflection["minMetallic"] = settings.hybridReflection.minMetallic;
    hybridReflection["hitOverlayEnabled"] = settings.hybridReflection.hitOverlayEnabled;
    hybridReflection["hitOverlayMode"] = settings.hybridReflection.hitOverlayMode;
    hybridReflection["hitOverlayIntensity"] = settings.hybridReflection.hitOverlayIntensity;
    hybridReflection["hitNormalSource"] = settings.hybridReflection.hitNormalSource;
    hybridReflection["contributionEnabled"] = settings.hybridReflection.contributionEnabled;
    hybridReflection["contributionIntensity"] = settings.hybridReflection.contributionIntensity;
    hybridReflection["contributionMaxDistance"] = settings.hybridReflection.contributionMaxDistance;

    json toneMap;
    toneMap["operatorIndex"] = settings.toneMap.operatorIndex;
    toneMap["exposure"] = settings.toneMap.exposure;
    toneMap["paperWhiteNits"] = settings.toneMap.paperWhiteNits;
    toneMap["maxDisplayNits"] = settings.toneMap.maxDisplayNits;

    json specularDebugLines;
    specularDebugLines["enabled"] = settings.specularDebugLines.enabled;
    specularDebugLines["lineLength"] = settings.specularDebugLines.lineLength;
    specularDebugLines["showViewRay"] = settings.specularDebugLines.showViewRay;
    specularDebugLines["showNormal"] = settings.specularDebugLines.showNormal;
    specularDebugLines["showReflection"] = settings.specularDebugLines.showReflection;

    json result;
    result["schemaVersion"] = SceneRendererSettings::kSchemaVersion;
    result["lighting"] = std::move(lighting);
    result["shadow"] = std::move(shadow);
    result["temporalUpscaler"] = std::move(temporalUpscaler);
    result["hybridReflection"] = std::move(hybridReflection);
    result["toneMap"] = std::move(toneMap);
    result["specularDebugLines"] = std::move(specularDebugLines);
    result["renderingPath"] = static_cast<int>(settings.renderingPath);
    result["renderViewMode"] = static_cast<int>(settings.renderViewMode);
    result["backBufferClearColor"] = Float4ToJson(settings.backBufferClearColor);
    result["lightingPassDebugGradient"] = settings.lightingPassDebugGradient;
    return result;
}

bool SceneRendererSettingsFromJson(const nlohmann::json& value,
                                   const SceneRendererSettings& defaults,
                                   SceneRendererSettings& settings,
                                   std::string* error)
{
    try
    {
        if (!value.is_object())
        {
            throw std::runtime_error("SceneRenderer settings JSON must be an object.");
        }

        SceneRendererSettings parsed = defaults;

        if (value.contains("lighting"))
        {
            const json& lighting = value.at("lighting");
            if (lighting.contains("lightDirection"))
                parsed.lighting.lightDirection =
                    Float3FromJson(lighting.at("lightDirection"), parsed.lighting.lightDirection);
            if (lighting.contains("lightColor"))
                parsed.lighting.lightColor = Float3FromJson(lighting.at("lightColor"), parsed.lighting.lightColor);
            parsed.lighting.iblIntensity = lighting.value("iblIntensity", parsed.lighting.iblIntensity);
            parsed.lighting.diffuseIntensity = lighting.value("diffuseIntensity", parsed.lighting.diffuseIntensity);
            parsed.lighting.skyboxEnabled = lighting.value("skyboxEnabled", parsed.lighting.skyboxEnabled);
            parsed.lighting.skyboxPreview = lighting.value("skyboxPreview", parsed.lighting.skyboxPreview);
            parsed.lighting.skyboxPreviewExposure =
                lighting.value("skyboxPreviewExposure", parsed.lighting.skyboxPreviewExposure);
            parsed.lighting.directLightEnabled =
                lighting.value("directLightEnabled", parsed.lighting.directLightEnabled);
            parsed.lighting.diffuseIblEnabled = lighting.value("diffuseIblEnabled", parsed.lighting.diffuseIblEnabled);
            parsed.lighting.specularIblEnabled =
                lighting.value("specularIblEnabled", parsed.lighting.specularIblEnabled);
            parsed.lighting.emissiveEnabled = lighting.value("emissiveEnabled", parsed.lighting.emissiveEnabled);
            parsed.lighting.iblDebugMip = lighting.value("iblDebugMip", parsed.lighting.iblDebugMip);
            parsed.lighting.iblDebugExposure = lighting.value("iblDebugExposure", parsed.lighting.iblDebugExposure);
        }

        if (value.contains("shadow"))
        {
            const json& shadow = value.at("shadow");
            parsed.shadow.enabled = shadow.value("enabled", parsed.shadow.enabled);
            parsed.shadow.normalBias = shadow.value("normalBias", parsed.shadow.normalBias);
            parsed.shadow.rayTMin = shadow.value("rayTMin", parsed.shadow.rayTMin);
            parsed.shadow.rayTMax = shadow.value("rayTMax", parsed.shadow.rayTMax);
            parsed.shadow.softShadowEnabled = shadow.value("softShadowEnabled", parsed.shadow.softShadowEnabled);
            parsed.shadow.sampleCount = shadow.value("sampleCount", parsed.shadow.sampleCount);
            parsed.shadow.lightAngularRadius = shadow.value("lightAngularRadius", parsed.shadow.lightAngularRadius);
            parsed.shadow.jitterStrength = shadow.value("jitterStrength", parsed.shadow.jitterStrength);
        }

        if (value.contains("temporalUpscaler"))
        {
            const json& temporal = value.at("temporalUpscaler");
            parsed.temporalUpscaler.enabled = temporal.value("enabled", parsed.temporalUpscaler.enabled);
            parsed.temporalUpscaler.backend = EnumValue(temporal, "backend", parsed.temporalUpscaler.backend);
            parsed.temporalUpscaler.qualityMode =
                EnumValue(temporal, "qualityMode", parsed.temporalUpscaler.qualityMode);
            parsed.temporalUpscaler.renderScale = temporal.value("renderScale", parsed.temporalUpscaler.renderScale);
            parsed.temporalUpscaler.sharpness = temporal.value("sharpness", parsed.temporalUpscaler.sharpness);
            parsed.temporalUpscaler.autoExposure = temporal.value("autoExposure", parsed.temporalUpscaler.autoExposure);
        }

        if (value.contains("hybridReflection"))
        {
            const json& reflection = value.at("hybridReflection");
            parsed.hybridReflection.enabled = reflection.value("enabled", parsed.hybridReflection.enabled);
            parsed.hybridReflection.materialGateEnabled =
                reflection.value("materialGateEnabled", parsed.hybridReflection.materialGateEnabled);
            parsed.hybridReflection.maxRoughness =
                reflection.value("maxRoughness", parsed.hybridReflection.maxRoughness);
            parsed.hybridReflection.minMetallic = reflection.value("minMetallic", parsed.hybridReflection.minMetallic);
            parsed.hybridReflection.hitOverlayEnabled =
                reflection.value("hitOverlayEnabled", parsed.hybridReflection.hitOverlayEnabled);
            parsed.hybridReflection.hitOverlayMode =
                reflection.value("hitOverlayMode", parsed.hybridReflection.hitOverlayMode);
            parsed.hybridReflection.hitOverlayIntensity =
                reflection.value("hitOverlayIntensity", parsed.hybridReflection.hitOverlayIntensity);
            parsed.hybridReflection.hitNormalSource =
                reflection.value("hitNormalSource", parsed.hybridReflection.hitNormalSource);
            parsed.hybridReflection.contributionEnabled =
                reflection.value("contributionEnabled", parsed.hybridReflection.contributionEnabled);
            parsed.hybridReflection.contributionIntensity =
                reflection.value("contributionIntensity", parsed.hybridReflection.contributionIntensity);
            parsed.hybridReflection.contributionMaxDistance =
                reflection.value("contributionMaxDistance", parsed.hybridReflection.contributionMaxDistance);
        }

        if (value.contains("toneMap"))
        {
            const json& toneMap = value.at("toneMap");
            parsed.toneMap.operatorIndex = toneMap.value("operatorIndex", parsed.toneMap.operatorIndex);
            parsed.toneMap.exposure = toneMap.value("exposure", parsed.toneMap.exposure);
            parsed.toneMap.paperWhiteNits = toneMap.value("paperWhiteNits", parsed.toneMap.paperWhiteNits);
            parsed.toneMap.maxDisplayNits = toneMap.value("maxDisplayNits", parsed.toneMap.maxDisplayNits);
        }

        if (value.contains("specularDebugLines"))
        {
            const json& debugLines = value.at("specularDebugLines");
            parsed.specularDebugLines.enabled = debugLines.value("enabled", parsed.specularDebugLines.enabled);
            parsed.specularDebugLines.lineLength = debugLines.value("lineLength", parsed.specularDebugLines.lineLength);
            parsed.specularDebugLines.showViewRay =
                debugLines.value("showViewRay", parsed.specularDebugLines.showViewRay);
            parsed.specularDebugLines.showNormal = debugLines.value("showNormal", parsed.specularDebugLines.showNormal);
            parsed.specularDebugLines.showReflection =
                debugLines.value("showReflection", parsed.specularDebugLines.showReflection);
        }

        parsed.renderingPath = EnumValue(value, "renderingPath", parsed.renderingPath);
        parsed.renderViewMode = EnumValue(value, "renderViewMode", parsed.renderViewMode);
        if (value.contains("backBufferClearColor"))
            parsed.backBufferClearColor = Float4FromJson(value.at("backBufferClearColor"), parsed.backBufferClearColor);
        parsed.lightingPassDebugGradient = value.value("lightingPassDebugGradient", parsed.lightingPassDebugGradient);

        settings = parsed;
        if (error != nullptr)
        {
            error->clear();
        }
        return true;
    }
    catch (const std::exception& exception)
    {
        if (error != nullptr)
        {
            *error = exception.what();
        }
        return false;
    }
}

std::string SerializeSceneRendererSettings(const SceneRendererSettings& settings, int indent)
{
    return SceneRendererSettingsToJson(settings).dump(indent);
}

bool DeserializeSceneRendererSettings(std::string_view jsonText, SceneRendererSettings& settings, std::string* error)
{
    try
    {
        const json value = json::parse(jsonText);
        return SceneRendererSettingsFromJson(value, settings, settings, error);
    }
    catch (const std::exception& exception)
    {
        if (error != nullptr)
        {
            *error = exception.what();
        }
        return false;
    }
}
} // namespace RtPbrSurvey
