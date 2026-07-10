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

// ---------------------------------------------------------------------------
// Sub-config JSON helpers
// ---------------------------------------------------------------------------

static json LightingToJson(const SceneLightingConfig& l)
{
    json j;
    j["lightDirection"] = ToJson(l.lightDirection);
    j["lightColor"] = ToJson(l.lightColor);
    j["iblIntensity"] = l.iblIntensity;
    j["diffuseIntensity"] = l.diffuseIntensity;
    j["iblDebugMip"] = l.iblDebugMip;
    j["iblDebugExposure"] = l.iblDebugExposure;
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
    l.iblDebugMip = j.value("iblDebugMip", 0.0f);
    l.iblDebugExposure = j.value("iblDebugExposure", 0.25f);
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

static json SpecularDebugLineToJson(const SceneSpecularDebugLineConfig& s)
{
    json j;
    j["enabled"] = s.enabled;
    j["lineLength"] = s.lineLength;
    j["showViewRay"] = s.showViewRay;
    j["showNormal"] = s.showNormal;
    j["showReflection"] = s.showReflection;
    return j;
}

static SceneSpecularDebugLineConfig SpecularDebugLineFromJson(const json& j)
{
    SceneSpecularDebugLineConfig s;
    s.enabled = j.value("enabled", true);
    s.lineLength = j.value("lineLength", 1.0f);
    s.showViewRay = j.value("showViewRay", true);
    s.showNormal = j.value("showNormal", true);
    s.showReflection = j.value("showReflection", true);
    return s;
}

// ---------------------------------------------------------------------------
// SceneConfig <-> json conversion
// ---------------------------------------------------------------------------

static json SceneConfigToJson(const SceneConfig& cfg)
{
    json j;
    j["camera"] = CameraToJson(cfg.camera);
    j["lighting"] = LightingToJson(cfg.lighting);
    j["environment"] = EnvironmentToJson(cfg.environment);
    j["toneMap"] = ToneMapToJson(cfg.toneMap);
    j["shadow"] = ShadowToJson(cfg.shadow);
    j["hybridReflection"] = HybridReflectionToJson(cfg.hybridReflection);
    j["specularDebugLines"] = SpecularDebugLineToJson(cfg.specularDebugLines);
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
    return j;
}

static SceneConfig SceneConfigFromJson(const json& j)
{
    SceneConfig cfg;
    if (j.contains("camera")) cfg.camera = CameraFromJson(j["camera"]);
    if (j.contains("lighting")) cfg.lighting = LightingFromJson(j["lighting"]);
    if (j.contains("environment")) cfg.environment = EnvironmentFromJson(j["environment"]);
    if (j.contains("toneMap")) cfg.toneMap = ToneMapFromJson(j["toneMap"]);
    if (j.contains("shadow")) cfg.shadow = ShadowFromJson(j["shadow"]);
    if (j.contains("hybridReflection")) cfg.hybridReflection = HybridReflectionFromJson(j["hybridReflection"]);
    if (j.contains("specularDebugLines")) cfg.specularDebugLines = SpecularDebugLineFromJson(j["specularDebugLines"]);
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
    return overrides.value();
}

// ---------------------------------------------------------------------------
// Capture: read current app/engine state into SceneConfig
// ---------------------------------------------------------------------------

SceneConfig SceneConfigManager::CaptureFromApp(
    const RtPbrSurveyApp& app,
    const RtPbrSurveyEngine& engine,
    const Engine::SampleScene& scene)
{
    SceneConfig cfg;
    const auto& camera = scene.GetScene().camera;
    cfg.camera.fov = camera.fov;

    if (app.m_cameraMode == RtPbrSurveyApp::CameraMode::Arcball)
    {
        cfg.camera.mode = "arcball";
        cfg.camera.yaw = app.m_objectViewerYaw;
        cfg.camera.pitch = app.m_objectViewerPitch;
        cfg.camera.distance = app.m_objectViewerDistance;
        cfg.camera.pivot = {
            app.m_objectViewerPivot.x,
            app.m_objectViewerPivot.y,
            app.m_objectViewerPivot.z,
        };
    }
    else
    {
        cfg.camera.mode = "freelook";
        cfg.camera.position = { camera.pos.x, camera.pos.y, camera.pos.z };
        cfg.camera.rotation = { camera.rot.x, camera.rot.y, camera.rot.z };
        cfg.camera.fov = camera.fov;
    }

    cfg.meshScale = app.m_meshScale;
    cfg.displayInstanceCount = scene.DisplayInstanceCount();
    cfg.selectedMaterialIndex = app.m_selectedMaterialIndex;
    cfg.isPlaying = app.m_isPlaying;

    // Lighting
    cfg.lighting.lightDirection = {
        app.m_lightingParams.lightDirection.x,
        app.m_lightingParams.lightDirection.y,
        app.m_lightingParams.lightDirection.z,
    };
    cfg.lighting.lightColor = {
        app.m_lightingParams.lightColor.x,
        app.m_lightingParams.lightColor.y,
        app.m_lightingParams.lightColor.z,
    };
    cfg.lighting.iblIntensity = app.m_lightingParams.iblIntensity;
    cfg.lighting.diffuseIntensity = app.m_lightingParams.diffuseIntensity;
    cfg.lighting.iblDebugMip = app.m_lightingParams.iblDebugMip;
    cfg.lighting.iblDebugExposure = app.m_lightingParams.iblDebugExposure;
    cfg.lighting.skyboxEnabled = app.m_lightingParams.skyboxEnabled;
    cfg.lighting.skyboxPreview = app.m_lightingParams.skyboxPreview;
    cfg.lighting.skyboxPreviewExposure = app.m_lightingParams.skyboxPreviewExposure;
    cfg.lighting.directLightEnabled = app.m_lightingParams.directLightEnabled;
    cfg.lighting.diffuseIblEnabled = app.m_lightingParams.diffuseIblEnabled;
    cfg.lighting.specularIblEnabled = app.m_lightingParams.specularIblEnabled;
    cfg.lighting.emissiveEnabled = app.m_lightingParams.emissiveEnabled;

    cfg.iblEnabled = app.m_iblEnabled;

    // Environment
    cfg.environment.source = static_cast<int>(app.m_environmentSettings.source);
    cfg.environment.skyColor = {
        app.m_environmentSettings.skyColor.x,
        app.m_environmentSettings.skyColor.y,
        app.m_environmentSettings.skyColor.z,
    };
    cfg.environment.groundColor = {
        app.m_environmentSettings.groundColor.x,
        app.m_environmentSettings.groundColor.y,
        app.m_environmentSettings.groundColor.z,
    };
    cfg.environment.lightColor = {
        app.m_environmentSettings.lightColor.x,
        app.m_environmentSettings.lightColor.y,
        app.m_environmentSettings.lightColor.z,
    };
    cfg.environment.lightDirection = {
        app.m_environmentSettings.lightDirection.x,
        app.m_environmentSettings.lightDirection.y,
        app.m_environmentSettings.lightDirection.z,
    };
    cfg.environment.backgroundIntensity = app.m_environmentSettings.backgroundIntensity;
    cfg.environment.lightIntensity = app.m_environmentSettings.lightIntensity;
    cfg.environment.lightSize = app.m_environmentSettings.lightSize;
    cfg.environment.fillIntensity = app.m_environmentSettings.fillIntensity;
    cfg.environment.colorPanelIntensity = app.m_environmentSettings.colorPanelIntensity;
    cfg.environment.horizonSharpness = app.m_environmentSettings.horizonSharpness;
    cfg.environmentAutoUpdate = app.m_environmentAutoUpdate;

    // Tone map
    cfg.toneMap.operatorIndex = app.m_toneMapParams.operatorIndex;
    cfg.toneMap.exposure = app.m_toneMapParams.exposure;
    cfg.toneMap.paperWhiteNits = app.m_toneMapParams.paperWhiteNits;
    cfg.toneMap.maxDisplayNits = app.m_toneMapParams.maxDisplayNits;

    // Rendering / debug
    cfg.renderingPath = static_cast<int>(app.m_renderingPath);
    cfg.renderViewMode = static_cast<int>(app.m_renderViewMode);
    cfg.lightingPassDebugGradient = app.m_lightingPassDebugGradient;
    cfg.backBufferClearColor = app.m_backBufferClearColor;

    // Engine state (no app member mirrors)
    {
        const auto& shadow = engine.GetShadowSettings();
        cfg.shadow.enabled = shadow.enabled;
        cfg.shadow.normalBias = shadow.normalBias;
        cfg.shadow.rayTMin = shadow.rayTMin;
        cfg.shadow.rayTMax = shadow.rayTMax;
        cfg.shadow.softShadowEnabled = shadow.softShadowEnabled;
        cfg.shadow.sampleCount = shadow.sampleCount;
        cfg.shadow.lightAngularRadius = shadow.lightAngularRadius;
        cfg.shadow.jitterStrength = shadow.jitterStrength;
    }
    {
        const auto& hr = engine.GetHybridReflectionSettings();
        cfg.hybridReflection.enabled = hr.enabled;
        cfg.hybridReflection.materialGateEnabled = hr.materialGateEnabled;
        cfg.hybridReflection.maxRoughness = hr.maxRoughness;
        cfg.hybridReflection.minMetallic = hr.minMetallic;
        cfg.hybridReflection.hitOverlayEnabled = hr.hitOverlayEnabled;
        cfg.hybridReflection.hitOverlayMode = hr.hitOverlayMode;
        cfg.hybridReflection.hitOverlayIntensity = hr.hitOverlayIntensity;
        cfg.hybridReflection.hitNormalSource = hr.hitNormalSource;
        cfg.hybridReflection.contributionEnabled = hr.contributionEnabled;
        cfg.hybridReflection.contributionIntensity = hr.contributionIntensity;
    }
    {
        const auto& sd = engine.GetSpecularDebugLineSettings();
        cfg.specularDebugLines.enabled = sd.enabled;
        cfg.specularDebugLines.lineLength = sd.lineLength;
        cfg.specularDebugLines.showViewRay = sd.showViewRay;
        cfg.specularDebugLines.showNormal = sd.showNormal;
        cfg.specularDebugLines.showReflection = sd.showReflection;
    }

    return cfg;
}

// ---------------------------------------------------------------------------
// Apply: write SceneConfig into app/engine state
// ---------------------------------------------------------------------------

void SceneConfigManager::ApplyToEngine(
    const SceneConfig& cfg,
    RtPbrSurveyApp& app,
    RtPbrSurveyEngine& engine)
{
    auto& camera = app.LoadedScene().GetScene().camera;
    camera.fov = cfg.camera.fov;

    // Camera mode
    if (cfg.camera.mode == "arcball")
    {
        app.m_cameraMode = RtPbrSurveyApp::CameraMode::Arcball;
        app.m_objectViewerYaw = cfg.camera.yaw;
        app.m_objectViewerPitch = cfg.camera.pitch;
        app.m_objectViewerDistance = cfg.camera.distance;
        app.m_objectViewerPivot.x = cfg.camera.pivot[0];
        app.m_objectViewerPivot.y = cfg.camera.pivot[1];
        app.m_objectViewerPivot.z = cfg.camera.pivot[2];
        app.UpdateObjectViewerCamera();
    }
    else
    {
        app.m_cameraMode = RtPbrSurveyApp::CameraMode::FreeLook;
        camera.pos.x = cfg.camera.position[0];
        camera.pos.y = cfg.camera.position[1];
        camera.pos.z = cfg.camera.position[2];
        camera.rot.x = cfg.camera.rotation[0];
        camera.rot.y = cfg.camera.rotation[1];
        camera.rot.z = cfg.camera.rotation[2];
        camera.fov = cfg.camera.fov;

        // Recompute gaze point
        const XMMATRIX camRot = XMMatrixRotationRollPitchYaw(camera.rot.x, camera.rot.y, camera.rot.z);
        const XMVECTOR fwd = XMVector3TransformNormal(XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f), camRot);
        XMStoreFloat3(&camera.gazePoint, XMLoadFloat3(&camera.pos) + fwd);
    }

    // Scene params
    app.m_meshScale = cfg.meshScale;
    app.m_selectedMaterialIndex = cfg.selectedMaterialIndex;
    app.m_isPlaying = cfg.isPlaying;
    app.LoadedScene().SetDisplayInstanceCount(cfg.displayInstanceCount);
    app.m_displayInstanceCount = app.LoadedScene().DisplayInstanceCount();
    engine.SetDisplayInstanceCount(app.m_displayInstanceCount);

    // Apply order follows doc/apply-order (critical for correctness)
    // 1. environmentAutoUpdate
    app.m_environmentAutoUpdate = cfg.environmentAutoUpdate;

    // 2. iblEnabled (master toggle before lighting)
    app.m_iblEnabled = cfg.iblEnabled;

    // 3. Lighting
    app.m_lightingParams.lightDirection.x = cfg.lighting.lightDirection[0];
    app.m_lightingParams.lightDirection.y = cfg.lighting.lightDirection[1];
    app.m_lightingParams.lightDirection.z = cfg.lighting.lightDirection[2];
    app.m_lightingParams.lightColor.x = cfg.lighting.lightColor[0];
    app.m_lightingParams.lightColor.y = cfg.lighting.lightColor[1];
    app.m_lightingParams.lightColor.z = cfg.lighting.lightColor[2];
    app.m_lightingParams.iblIntensity = cfg.lighting.iblIntensity;
    app.m_lightingParams.diffuseIntensity = cfg.lighting.diffuseIntensity;
    app.m_lightingParams.iblDebugMip = cfg.lighting.iblDebugMip;
    app.m_lightingParams.iblDebugExposure = cfg.lighting.iblDebugExposure;
    app.m_lightingParams.skyboxEnabled = cfg.lighting.skyboxEnabled;
    app.m_lightingParams.skyboxPreview = cfg.lighting.skyboxPreview;
    app.m_lightingParams.skyboxPreviewExposure = cfg.lighting.skyboxPreviewExposure;
    app.m_lightingParams.directLightEnabled = cfg.lighting.directLightEnabled;
    app.m_lightingParams.diffuseIblEnabled = cfg.lighting.diffuseIblEnabled;
    app.m_lightingParams.specularIblEnabled = cfg.lighting.specularIblEnabled;
    app.m_lightingParams.emissiveEnabled = cfg.lighting.emissiveEnabled;

    // 4. Environment (triggers GPU reload)
    app.m_environmentSettings.source = static_cast<Engine::EnvironmentSource>(cfg.environment.source);
    app.m_environmentSettings.skyColor.x = cfg.environment.skyColor[0];
    app.m_environmentSettings.skyColor.y = cfg.environment.skyColor[1];
    app.m_environmentSettings.skyColor.z = cfg.environment.skyColor[2];
    app.m_environmentSettings.groundColor.x = cfg.environment.groundColor[0];
    app.m_environmentSettings.groundColor.y = cfg.environment.groundColor[1];
    app.m_environmentSettings.groundColor.z = cfg.environment.groundColor[2];
    app.m_environmentSettings.lightColor.x = cfg.environment.lightColor[0];
    app.m_environmentSettings.lightColor.y = cfg.environment.lightColor[1];
    app.m_environmentSettings.lightColor.z = cfg.environment.lightColor[2];
    app.m_environmentSettings.lightDirection.x = cfg.environment.lightDirection[0];
    app.m_environmentSettings.lightDirection.y = cfg.environment.lightDirection[1];
    app.m_environmentSettings.lightDirection.z = cfg.environment.lightDirection[2];
    app.m_environmentSettings.backgroundIntensity = cfg.environment.backgroundIntensity;
    app.m_environmentSettings.lightIntensity = cfg.environment.lightIntensity;
    app.m_environmentSettings.lightSize = cfg.environment.lightSize;
    app.m_environmentSettings.fillIntensity = cfg.environment.fillIntensity;
    app.m_environmentSettings.colorPanelIntensity = cfg.environment.colorPanelIntensity;
    app.m_environmentSettings.horizonSharpness = cfg.environment.horizonSharpness;
    engine.ReloadEnvironmentResources(app.m_environmentSettings);

    // 5. Shadow
    {
        RtPbrSurveyEngine::ShadowSettings shadow;
        shadow.enabled = cfg.shadow.enabled;
        shadow.normalBias = cfg.shadow.normalBias;
        shadow.rayTMin = cfg.shadow.rayTMin;
        shadow.rayTMax = cfg.shadow.rayTMax;
        shadow.softShadowEnabled = cfg.shadow.softShadowEnabled;
        shadow.sampleCount = cfg.shadow.sampleCount;
        shadow.lightAngularRadius = cfg.shadow.lightAngularRadius;
        shadow.jitterStrength = cfg.shadow.jitterStrength;
        engine.SetShadowSettings(shadow);
    }

    // 6. Hybrid reflection
    {
        RtPbrSurveyEngine::HybridReflectionSettings hr;
        hr.enabled = cfg.hybridReflection.enabled;
        hr.materialGateEnabled = cfg.hybridReflection.materialGateEnabled;
        hr.maxRoughness = cfg.hybridReflection.maxRoughness;
        hr.minMetallic = cfg.hybridReflection.minMetallic;
        hr.hitOverlayEnabled = cfg.hybridReflection.hitOverlayEnabled;
        hr.hitOverlayMode = cfg.hybridReflection.hitOverlayMode;
        hr.hitOverlayIntensity = cfg.hybridReflection.hitOverlayIntensity;
        hr.hitNormalSource = cfg.hybridReflection.hitNormalSource;
        hr.contributionEnabled = cfg.hybridReflection.contributionEnabled;
        hr.contributionIntensity = cfg.hybridReflection.contributionIntensity;
        engine.SetHybridReflectionSettings(hr);
    }

    // 7. Specular debug lines
    {
        RtPbrSurveyEngine::SpecularDebugLineSettings sd;
        sd.enabled = cfg.specularDebugLines.enabled;
        sd.lineLength = cfg.specularDebugLines.lineLength;
        sd.showViewRay = cfg.specularDebugLines.showViewRay;
        sd.showNormal = cfg.specularDebugLines.showNormal;
        sd.showReflection = cfg.specularDebugLines.showReflection;
        engine.SetSpecularDebugLineSettings(sd);
    }

    // 8. Tone map
    app.m_toneMapParams.operatorIndex = cfg.toneMap.operatorIndex;
    app.m_toneMapParams.exposure = cfg.toneMap.exposure;
    app.m_toneMapParams.paperWhiteNits = cfg.toneMap.paperWhiteNits;
    app.m_toneMapParams.maxDisplayNits = cfg.toneMap.maxDisplayNits;

    // 9. Rendering path
    app.m_renderingPath = static_cast<RtPbrSurveyEngine::RenderingPath>(cfg.renderingPath);

    // 10. Render view mode
    app.m_renderViewMode = static_cast<RtPbrSurveyEngine::RenderViewMode>(cfg.renderViewMode);

    // 11. Lighting pass debug gradient
    app.m_lightingPassDebugGradient = cfg.lightingPassDebugGradient;

    // 12. Back buffer clear color
    app.m_backBufferClearColor = cfg.backBufferClearColor;
}

// ---------------------------------------------------------------------------
// Load / Save / Reset orchestration
// ---------------------------------------------------------------------------

void SceneConfigManager::LoadAndApplyForScene(
    int sceneIndex,
    RtPbrSurveyApp& app,
    RtPbrSurveyEngine& engine,
    const Engine::SampleScene& scene)
{
    ReadDefaultsFromDisk();
    ReadUserConfigFromDisk();

    const std::string name = scene.Name();
    SceneConfig defaults = CaptureFromApp(app, engine, scene);
    defaults.sceneName = name;

    auto defaultEntry = FindEntry(m_defaults, name);
    auto userEntry = FindEntry(m_userOverrides, name);

    SceneConfig merged = Merge(
        defaultEntry.value_or(defaults),
        userEntry);

    ApplyToEngine(merged, app, engine);
}

void SceneConfigManager::SaveCurrentScene(
    int sceneIndex,
    const RtPbrSurveyApp& app,
    const RtPbrSurveyEngine& engine,
    const Engine::SampleScene& scene)
{
    ReadUserConfigFromDisk();

    const std::string name = scene.Name();
    m_userOverrides[name] = CaptureFromApp(app, engine, scene);
    m_userOverrides[name].sceneName = name;

    WriteUserConfigToDisk();
}

void SceneConfigManager::LoadDefaultsForScene(
    int sceneIndex,
    RtPbrSurveyApp& app,
    RtPbrSurveyEngine& engine,
    const Engine::SampleScene& scene)
{
    ReadDefaultsFromDisk();

    const std::string name = scene.Name();
    auto defaultEntry = FindEntry(m_defaults, name);
    if (!defaultEntry.has_value())
    {
        return;
    }

    ApplyToEngine(defaultEntry.value(), app, engine);
}

void SceneConfigManager::ResetCurrentScene(
    int sceneIndex,
    RtPbrSurveyApp& app,
    RtPbrSurveyEngine& engine,
    const Engine::SampleScene& scene)
{
    ReadUserConfigFromDisk();
    ReadDefaultsFromDisk();

    const std::string name = scene.Name();
    m_userOverrides.erase(name);
    WriteUserConfigToDisk();

    auto defaultEntry = FindEntry(m_defaults, name);
    if (!defaultEntry.has_value())
    {
        return;
    }

    ApplyToEngine(defaultEntry.value(), app, engine);
}

void SceneConfigManager::ResetAllScenes(
    RtPbrSurveyApp& app,
    RtPbrSurveyEngine& engine,
    const Engine::SampleScene& currentScene)
{
    m_userOverrides.clear();

    // Delete the user config file
    DeleteFileA(m_userConfigPath.c_str());
    // Also clean up any leftover temp file
    DeleteFileA((m_userConfigPath + ".tmp").c_str());

    ReadDefaultsFromDisk();

    const std::string name = currentScene.Name();
    auto defaultEntry = FindEntry(m_defaults, name);
    if (!defaultEntry.has_value())
    {
        return;
    }

    ApplyToEngine(defaultEntry.value(), app, engine);
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
