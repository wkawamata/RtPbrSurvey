#include "stdafx.h"
#include "SceneConfig.h"
#include "App/RtPbrSurveyApp.h"
#include "Engine/RtPbrSurveyEngine.h"

#include <fstream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace App
{

// ---------------------------------------------------------------------------
// JSON serialization helpers
// ---------------------------------------------------------------------------

static json ToJson(const std::array<float, 3>& a)
{
    return json{a[0], a[1], a[2]};
}

static json ToJson(const std::array<float, 4>& a)
{
    return json{a[0], a[1], a[2], a[3]};
}

static std::array<float, 3> Array3FromJson(const json& j)
{
    return {j[0].get<float>(), j[1].get<float>(), j[2].get<float>()};
}

static std::array<float, 4> Array4FromJson(const json& j)
{
    return {j[0].get<float>(), j[1].get<float>(), j[2].get<float>(), j[3].get<float>()};
}

static json CameraToJson(const SceneCameraConfig& c)
{
    json j;
    j["mode"] = c.mode;
    if (c.mode == "arcball")
    {
        j["yaw"] = c.yaw;
        j["pitch"] = c.pitch;
        j["distance"] = c.distance;
        j["pivot"] = ToJson(c.pivot);
    }
    else
    {
        j["position"] = ToJson(c.position);
        j["rotation"] = ToJson(c.rotation);
    }
    j["fov"] = c.fov;
    return j;
}

static SceneCameraConfig CameraFromJson(const json& j)
{
    SceneCameraConfig c;
    c.mode = j.value("mode", "freelook");
    c.fov = j.value("fov", 60.0f);
    if (c.mode == "arcball")
    {
        c.yaw = j.value("yaw", 0.0f);
        c.pitch = j.value("pitch", 0.0f);
        c.distance = j.value("distance", 5.0f);
        if (j.contains("pivot")) c.pivot = Array3FromJson(j["pivot"]);
    }
    else
    {
        if (j.contains("position")) c.position = Array3FromJson(j["position"]);
        if (j.contains("rotation")) c.rotation = Array3FromJson(j["rotation"]);
    }
    return c;
}

static json LightingToJson(const SceneLightingConfig& l)
{
    json j;
    j["lightDirection"] = ToJson(l.lightDirection);
    j["lightColor"] = ToJson(l.lightColor);
    j["iblIntensity"] = l.iblIntensity;
    j["diffuseIntensity"] = l.diffuseIntensity;
    j["skyboxEnabled"] = l.skyboxEnabled;
    j["skyboxPreview"] = l.skyboxPreview;
    j["skyboxPreviewExposure"] = l.skyboxPreviewExposure;
    j["directLightEnabled"] = l.directLightEnabled;
    j["diffuseIblEnabled"] = l.diffuseIblEnabled;
    j["specularIblEnabled"] = l.specularIblEnabled;
    j["emissiveEnabled"] = l.emissiveEnabled;
    return j;
}

static SceneLightingConfig LightingFromJson(const json& j)
{
    SceneLightingConfig l;
    if (j.contains("lightDirection")) l.lightDirection = Array3FromJson(j["lightDirection"]);
    if (j.contains("lightColor")) l.lightColor = Array3FromJson(j["lightColor"]);
    l.iblIntensity = j.value("iblIntensity", 0.10f);
    l.diffuseIntensity = j.value("diffuseIntensity", 1.0f);
    l.skyboxEnabled = j.value("skyboxEnabled", true);
    l.skyboxPreview = j.value("skyboxPreview", false);
    l.skyboxPreviewExposure = j.value("skyboxPreviewExposure", 1.0f);
    l.directLightEnabled = j.value("directLightEnabled", true);
    l.diffuseIblEnabled = j.value("diffuseIblEnabled", true);
    l.specularIblEnabled = j.value("specularIblEnabled", true);
    l.emissiveEnabled = j.value("emissiveEnabled", true);
    return l;
}

static json EnvironmentToJson(const SceneEnvironmentConfig& e)
{
    json j;
    j["source"] = e.source;
    j["skyColor"] = ToJson(e.skyColor);
    j["groundColor"] = ToJson(e.groundColor);
    j["lightColor"] = ToJson(e.lightColor);
    j["lightDirection"] = ToJson(e.lightDirection);
    j["backgroundIntensity"] = e.backgroundIntensity;
    j["lightIntensity"] = e.lightIntensity;
    j["lightSize"] = e.lightSize;
    j["fillIntensity"] = e.fillIntensity;
    j["colorPanelIntensity"] = e.colorPanelIntensity;
    j["horizonSharpness"] = e.horizonSharpness;
    return j;
}

static SceneEnvironmentConfig EnvironmentFromJson(const json& j)
{
    SceneEnvironmentConfig e;
    e.source = j.value("source", 0);
    if (j.contains("skyColor")) e.skyColor = Array3FromJson(j["skyColor"]);
    if (j.contains("groundColor")) e.groundColor = Array3FromJson(j["groundColor"]);
    if (j.contains("lightColor")) e.lightColor = Array3FromJson(j["lightColor"]);
    if (j.contains("lightDirection")) e.lightDirection = Array3FromJson(j["lightDirection"]);
    e.backgroundIntensity = j.value("backgroundIntensity", 0.6f);
    e.lightIntensity = j.value("lightIntensity", 6.0f);
    e.lightSize = j.value("lightSize", 0.12f);
    e.fillIntensity = j.value("fillIntensity", 0.12f);
    e.colorPanelIntensity = j.value("colorPanelIntensity", 1.5f);
    e.horizonSharpness = j.value("horizonSharpness", 0.08f);
    return e;
}

static json ToneMapToJson(const SceneToneMapConfig& t)
{
    json j;
    j["operatorIndex"] = t.operatorIndex;
    j["exposure"] = t.exposure;
    j["paperWhiteNits"] = t.paperWhiteNits;
    j["maxDisplayNits"] = t.maxDisplayNits;
    return j;
}

static SceneToneMapConfig ToneMapFromJson(const json& j)
{
    SceneToneMapConfig t;
    t.operatorIndex = j.value("operatorIndex", 0);
    t.exposure = j.value("exposure", 1.0f);
    t.paperWhiteNits = j.value("paperWhiteNits", 300.0f);
    t.maxDisplayNits = j.value("maxDisplayNits", 1000.0f);
    return t;
}

static json ShadowToJson(const SceneShadowConfig& s)
{
    json j;
    j["enabled"] = s.enabled;
    j["normalBias"] = s.normalBias;
    j["rayTMin"] = s.rayTMin;
    j["rayTMax"] = s.rayTMax;
    j["softShadowEnabled"] = s.softShadowEnabled;
    j["sampleCount"] = s.sampleCount;
    j["lightAngularRadius"] = s.lightAngularRadius;
    j["jitterStrength"] = s.jitterStrength;
    return j;
}

static SceneShadowConfig ShadowFromJson(const json& j)
{
    SceneShadowConfig s;
    s.enabled = j.value("enabled", true);
    s.normalBias = j.value("normalBias", 0.01f);
    s.rayTMin = j.value("rayTMin", 0.001f);
    s.rayTMax = j.value("rayTMax", 10000.0f);
    s.softShadowEnabled = j.value("softShadowEnabled", true);
    s.sampleCount = j.value("sampleCount", 8);
    s.lightAngularRadius = j.value("lightAngularRadius", 0.1f);
    s.jitterStrength = j.value("jitterStrength", 2.0f);
    return s;
}

static json HybridReflectionToJson(const SceneHybridReflectionConfig& h)
{
    json j;
    j["enabled"] = h.enabled;
    j["materialGateEnabled"] = h.materialGateEnabled;
    j["maxRoughness"] = h.maxRoughness;
    j["minMetallic"] = h.minMetallic;
    j["hitOverlayEnabled"] = h.hitOverlayEnabled;
    j["hitOverlayMode"] = h.hitOverlayMode;
    j["hitOverlayIntensity"] = h.hitOverlayIntensity;
    j["hitNormalSource"] = h.hitNormalSource;
    j["contributionEnabled"] = h.contributionEnabled;
    j["contributionIntensity"] = h.contributionIntensity;
    return j;
}

static SceneHybridReflectionConfig HybridReflectionFromJson(const json& j)
{
    SceneHybridReflectionConfig h;
    h.enabled = j.value("enabled", true);
    h.materialGateEnabled = j.value("materialGateEnabled", false);
    h.maxRoughness = j.value("maxRoughness", 0.35f);
    h.minMetallic = j.value("minMetallic", 0.0f);
    h.hitOverlayEnabled = j.value("hitOverlayEnabled", false);
    h.hitOverlayMode = j.value("hitOverlayMode", 0);
    h.hitOverlayIntensity = j.value("hitOverlayIntensity", 0.2f);
    h.hitNormalSource = j.value("hitNormalSource", 0);
    h.contributionEnabled = j.value("contributionEnabled", false);
    h.contributionIntensity = j.value("contributionIntensity", 0.25f);
    return h;
}

static json SpecularDebugLineToJson(const SceneSpecularDebugLineConfig& d)
{
    json j;
    j["enabled"] = d.enabled;
    j["lineLength"] = d.lineLength;
    j["showViewRay"] = d.showViewRay;
    j["showNormal"] = d.showNormal;
    j["showReflection"] = d.showReflection;
    return j;
}

static SceneSpecularDebugLineConfig SpecularDebugLineFromJson(const json& j)
{
    SceneSpecularDebugLineConfig d;
    d.enabled = j.value("enabled", true);
    d.lineLength = j.value("lineLength", 1.0f);
    d.showViewRay = j.value("showViewRay", true);
    d.showNormal = j.value("showNormal", true);
    d.showReflection = j.value("showReflection", true);
    return d;
}

// ---------------------------------------------------------------------------
// SceneConfig <-> json conversion
// ---------------------------------------------------------------------------

static json SceneConfigToJson(const SceneConfig& cfg)
{
    json j;
    j["camera"] = CameraToJson(cfg.camera);
    j["meshScale"] = cfg.meshScale;
    j["displayInstanceCount"] = cfg.displayInstanceCount;
    j["selectedMaterialIndex"] = cfg.selectedMaterialIndex;
    j["isPlaying"] = cfg.isPlaying;
    j["renderingPath"] = cfg.renderingPath;
    j["renderViewMode"] = cfg.renderViewMode;
    j["lightingPassDebugGradient"] = cfg.lightingPassDebugGradient;
    j["backBufferClearColor"] = ToJson(cfg.backBufferClearColor);
    j["iblEnabled"] = cfg.iblEnabled;
    j["environmentAutoUpdate"] = cfg.environmentAutoUpdate;
    j["lighting"] = LightingToJson(cfg.lighting);
    j["environment"] = EnvironmentToJson(cfg.environment);
    j["toneMap"] = ToneMapToJson(cfg.toneMap);
    j["shadow"] = ShadowToJson(cfg.shadow);
    j["hybridReflection"] = HybridReflectionToJson(cfg.hybridReflection);
    j["specularDebugLines"] = SpecularDebugLineToJson(cfg.specularDebugLines);
    return j;
}

static SceneConfig SceneConfigFromJson(const json& j)
{
    SceneConfig cfg;
    if (j.contains("camera")) cfg.camera = CameraFromJson(j["camera"]);
    cfg.meshScale = j.value("meshScale", 0.5f);
    cfg.displayInstanceCount = j.value("displayInstanceCount", 1);
    cfg.selectedMaterialIndex = j.value("selectedMaterialIndex", 0);
    cfg.isPlaying = j.value("isPlaying", false);
    cfg.renderingPath = j.value("renderingPath", 1);
    cfg.renderViewMode = j.value("renderViewMode", 0);
    cfg.lightingPassDebugGradient = j.value("lightingPassDebugGradient", false);
    if (j.contains("backBufferClearColor")) cfg.backBufferClearColor = Array4FromJson(j["backBufferClearColor"]);
    cfg.iblEnabled = j.value("iblEnabled", true);
    cfg.environmentAutoUpdate = j.value("environmentAutoUpdate", true);
    if (j.contains("lighting")) cfg.lighting = LightingFromJson(j["lighting"]);
    if (j.contains("environment")) cfg.environment = EnvironmentFromJson(j["environment"]);
    if (j.contains("toneMap")) cfg.toneMap = ToneMapFromJson(j["toneMap"]);
    if (j.contains("shadow")) cfg.shadow = ShadowFromJson(j["shadow"]);
    if (j.contains("hybridReflection")) cfg.hybridReflection = HybridReflectionFromJson(j["hybridReflection"]);
    if (j.contains("specularDebugLines")) cfg.specularDebugLines = SpecularDebugLineFromJson(j["specularDebugLines"]);
    return cfg;
}

// ---------------------------------------------------------------------------
// SceneConfigManager implementation
// ---------------------------------------------------------------------------

SceneConfigManager::SceneConfigManager() = default;

void SceneConfigManager::SetPaths(const std::string& defaultsPath, const std::string& userConfigPath)
{
    m_defaultsPath = defaultsPath;
    m_userConfigPath = userConfigPath;
}

void SceneConfigManager::ReadDefaultsFromDisk()
{
    m_defaults.clear();

    std::ifstream file(m_defaultsPath);
    if (!file.is_open())
    {
        return; // No defaults file -- that's fine
    }

    json root;
    try
    {
        file >> root;
    }
    catch (...)
    {
        return;
    }

    if (!root.contains("scenes") || !root["scenes"].is_object())
    {
        return;
    }

    for (auto& [key, value] : root["scenes"].items())
    {
        m_defaults[key] = SceneConfigFromJson(value);
        m_defaults[key].sceneName = key;
    }
}

void SceneConfigManager::ReadUserConfigFromDisk()
{
    m_userOverrides.clear();

    std::ifstream file(m_userConfigPath);
    if (!file.is_open())
    {
        return;
    }

    json root;
    try
    {
        file >> root;
    }
    catch (...)
    {
        return;
    }

    if (!root.contains("scenes") || !root["scenes"].is_object())
    {
        return;
    }

    for (auto& [key, value] : root["scenes"].items())
    {
        m_userOverrides[key] = SceneConfigFromJson(value);
        m_userOverrides[key].sceneName = key;
    }
}

void SceneConfigManager::WriteUserConfigToDisk()
{
    json root;
    root["version"] = 1;
    root["scenes"] = json::object();

    for (auto& [name, cfg] : m_userOverrides)
    {
        root["scenes"][name] = SceneConfigToJson(cfg);
    }

    // Write to temp file first, then rename for crash safety
    const std::string tmpPath = m_userConfigPath + ".tmp";

    {
        std::ofstream file(tmpPath);
        if (!file.is_open())
        {
            return;
        }
        file << root.dump(2);
        file.close();
    }

    MoveFileExA(tmpPath.c_str(), m_userConfigPath.c_str(), MOVEFILE_REPLACE_EXISTING);
}

std::optional<SceneConfig> SceneConfigManager::FindEntry(
    const std::unordered_map<std::string, SceneConfig>& configs,
    const std::string& sceneName) const
{
    auto it = configs.find(sceneName);
    if (it != configs.end())
    {
        return it->second;
    }
    return std::nullopt;
}

SceneConfig SceneConfigManager::Merge(
    const SceneConfig& defaults,
    const std::optional<SceneConfig>& overrides)
{
    if (!overrides.has_value())
    {
        return defaults;
    }
    // Overrides wins -- merge is trivial since we always write full SceneConfig on save
    // In v1, the override is a complete SceneConfig, so just return it.
    return overrides.value();
}

ConfigSource SceneConfigManager::ActiveSource(const std::string& sceneName) const
{
    if (m_userOverrides.find(sceneName) != m_userOverrides.end())
    {
        return ConfigSource::UserFile;
    }
    if (m_defaults.find(sceneName) != m_defaults.end())
    {
        return ConfigSource::DefaultFile;
    }
    return ConfigSource::CodeDefaults;
}

} // namespace App
