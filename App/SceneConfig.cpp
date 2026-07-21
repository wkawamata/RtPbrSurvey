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

static std::array<float, 3> Array3FromJson(const json& j)
{
    return {j[0].get<float>(), j[1].get<float>(), j[2].get<float>()};
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
    j["speedMultiplier"] = c.speedMultiplier;
    j["nearZ"] = c.nearZ;
    j["farZ"] = c.farZ;
    return j;
}

static SceneCameraConfig CameraFromJson(const json& j)
{
    SceneCameraConfig c;
    c.mode = j.value("mode", "freelook");
    c.fov = j.value("fov", 60.0f);
    c.speedMultiplier = j.value("speedMultiplier", 1.0f);
    c.nearZ = j.value("nearZ", 0.1f);
    c.farZ = j.value("farZ", 10000.0f);
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

// ---------------------------------------------------------------------------
// SceneConfig <-> json conversion
// ---------------------------------------------------------------------------

static json SceneConfigToJson(const SceneConfig& cfg)
{
    const json renderer = RtPbrSurvey::SceneRendererSettingsToJson(cfg.renderer);
    json j;
    j["camera"] = CameraToJson(cfg.camera);
    j["lighting"] = renderer.at("lighting");
    j["environment"] = EnvironmentToJson(cfg.environment);
    j["toneMap"] = renderer.at("toneMap");
    j["shadow"] = renderer.at("shadow");
    j["temporalUpscaler"] = renderer.at("temporalUpscaler");
    j["hybridReflection"] = renderer.at("hybridReflection");
    j["specularDebugLines"] = renderer.at("specularDebugLines");
    j["meshScale"] = cfg.meshScale;
    j["displayInstanceCount"] = cfg.displayInstanceCount;
    j["selectedMaterialIndex"] = cfg.selectedMaterialIndex;
    j["isPlaying"] = cfg.isPlaying;
    j["renderingPath"] = renderer.at("renderingPath");
    j["renderViewMode"] = renderer.at("renderViewMode");
    j["lightingPassDebugGradient"] = renderer.at("lightingPassDebugGradient");
    j["backBufferClearColor"] = renderer.at("backBufferClearColor");
    j["rendererSchemaVersion"] = renderer.at("schemaVersion");
    j["iblEnabled"] = cfg.iblEnabled;
    j["environmentAutoUpdate"] = cfg.environmentAutoUpdate;
    return j;
}

static SceneConfig SceneConfigFromJson(const json& j)
{
    SceneConfig cfg;
    if (j.contains("camera")) cfg.camera = CameraFromJson(j["camera"]);
    if (j.contains("environment")) cfg.environment = EnvironmentFromJson(j["environment"]);

    json renderer = json::object();
    static constexpr const char* rendererKeys[] = {
        "lighting",
        "shadow",
        "temporalUpscaler",
        "hybridReflection",
        "toneMap",
        "specularDebugLines",
        "renderingPath",
        "renderViewMode",
        "backBufferClearColor",
        "lightingPassDebugGradient",
    };
    for (const char* key : rendererKeys)
    {
        if (j.contains(key))
        {
            renderer[key] = j.at(key);
        }
    }
    if (j.contains("rendererSchemaVersion"))
    {
        renderer["schemaVersion"] = j.at("rendererSchemaVersion");
    }
    RtPbrSurvey::SceneRendererSettingsFromJson(renderer, cfg.renderer, cfg.renderer);

    cfg.meshScale = j.value("meshScale", cfg.meshScale);
    cfg.displayInstanceCount = j.value("displayInstanceCount", cfg.displayInstanceCount);
    cfg.selectedMaterialIndex = j.value("selectedMaterialIndex", cfg.selectedMaterialIndex);
    cfg.isPlaying = j.value("isPlaying", cfg.isPlaying);
    cfg.iblEnabled = j.value("iblEnabled", cfg.iblEnabled);
    cfg.environmentAutoUpdate = j.value("environmentAutoUpdate", cfg.environmentAutoUpdate);
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

std::optional<SceneConfig> SceneConfigManager::FindEntry(const std::unordered_map<std::string, SceneConfig>& configs,
                                                         const std::string& sceneName) const
{
    auto it = configs.find(sceneName);
    if (it != configs.end())
    {
        return it->second;
    }
    return std::nullopt;
}

std::optional<SceneConfig> SceneConfigManager::FindEntryByIndex(
    const std::unordered_map<std::string, SceneConfig>& configs, const Engine::SampleScene& scene, int sceneIndex) const
{
    auto entry = FindEntry(configs, SceneConfigKey(scene, sceneIndex));
    if (entry.has_value())
    {
        return entry;
    }

    if (dynamic_cast<const Engine::AnimatedShadowGridScene*>(&scene) != nullptr)
    {
        return FindEntry(configs, "Animated Shadow Grid");
    }
    if (dynamic_cast<const Engine::ContactShadowTestScene*>(&scene) != nullptr)
    {
        return FindEntry(configs, "Contact Shadow Test");
    }
    if (dynamic_cast<const Engine::OccluderWallTestScene*>(&scene) != nullptr)
    {
        return FindEntry(configs, "Occluder Wall Test");
    }

    return std::nullopt;
}

std::string SceneConfigManager::SceneConfigKey(const Engine::SampleScene& scene, int /*sceneIndex*/) const
{
    const std::string name = scene.Name();
    if (dynamic_cast<const Engine::GltfGridBenchmarkScene*>(&scene) != nullptr)
    {
        return "Grid/" + name;
    }

    return name;
}

SceneConfig SceneConfigManager::Merge(const SceneConfig& defaults, const std::optional<SceneConfig>& overrides)
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

SceneConfig SceneConfigManager::CaptureFromApp(const RtPbrSurveyApp& app,
                                               const RtPbrSurveyEngine& engine,
                                               const Engine::SampleScene& scene)
{
    SceneConfig cfg;
    const auto& camera = scene.GetScene().camera;
    cfg.camera.fov = camera.fov;

    if (app.DebugCamera().GetMode() == RtPbrSurvey::DebugCameraController::Mode::Arcball)
    {
        const XMFLOAT3 pivot = app.DebugCamera().ObjectViewerPivot();
        cfg.camera.mode = "arcball";
        cfg.camera.yaw = app.DebugCamera().ObjectViewerYaw();
        cfg.camera.pitch = app.DebugCamera().ObjectViewerPitch();
        cfg.camera.distance = app.DebugCamera().ObjectViewerDistance();
        cfg.camera.pivot = {
            pivot.x,
            pivot.y,
            pivot.z,
        };
    }
    else
    {
        cfg.camera.mode = "freelook";
        cfg.camera.position = {camera.pos.x, camera.pos.y, camera.pos.z};
        cfg.camera.rotation = {camera.rot.x, camera.rot.y, camera.rot.z};
        cfg.camera.fov = camera.fov;
    }

    cfg.camera.speedMultiplier = app.DebugCamera().SpeedMultiplier();
    cfg.camera.nearZ = camera.nearZ;
    cfg.camera.farZ = camera.farZ;

    cfg.meshScale = app.m_meshScale;
    cfg.displayInstanceCount = scene.DisplayInstanceCount();
    cfg.selectedMaterialIndex = app.m_selectedMaterialIndex;
    cfg.isPlaying = app.m_isPlaying;

    cfg.renderer = app.m_sceneRenderer.CaptureSettings();
    // App controls use local mirrors until the end of the ImGui frame.
    cfg.renderer.lighting = app.m_lightingParams;
    cfg.renderer.toneMap = app.m_toneMapParams;
    cfg.renderer.renderingPath = app.m_renderingPath;
    cfg.renderer.renderViewMode = app.m_renderViewMode;
    cfg.renderer.lightingPassDebugGradient = app.m_lightingPassDebugGradient;
    cfg.renderer.backBufferClearColor = app.m_backBufferClearColor;

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

    return cfg;
}

// ---------------------------------------------------------------------------
// Apply: write SceneConfig into app/engine state
// ---------------------------------------------------------------------------

void SceneConfigManager::ApplyToEngine(const SceneConfig& cfg, RtPbrSurveyApp& app, RtPbrSurveyEngine& engine)
{
    auto& camera = app.LoadedScene().GetScene().camera;
    camera.fov = cfg.camera.fov;
    camera.nearZ = cfg.camera.nearZ;
    camera.farZ = cfg.camera.farZ;
    app.DebugCamera().SetSpeedMultiplier(cfg.camera.speedMultiplier);

    // Camera mode
    if (cfg.camera.mode == "arcball")
    {
        app.DebugCamera().SetMode(RtPbrSurvey::DebugCameraController::Mode::Arcball);
        app.DebugCamera().SetObjectViewerState(cfg.camera.yaw,
                                               cfg.camera.pitch,
                                               cfg.camera.distance,
                                               XMFLOAT3{cfg.camera.pivot[0], cfg.camera.pivot[1], cfg.camera.pivot[2]});
    }
    else
    {
        app.DebugCamera().SetMode(RtPbrSurvey::DebugCameraController::Mode::FreeLook);
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
    int displayInstanceCount = cfg.displayInstanceCount;
    if (dynamic_cast<Engine::AnimatedShadowGridScene*>(&app.LoadedScene()) != nullptr && displayInstanceCount <= 1)
    {
        displayInstanceCount = app.LoadedScene().MaxDisplayInstanceCount();
    }
    app.LoadedScene().SetDisplayInstanceCount(displayInstanceCount);
    app.m_displayInstanceCount = app.LoadedScene().DisplayInstanceCount();
    engine.SetDisplayInstanceCount(app.m_displayInstanceCount);

    // Apply order follows doc/apply-order (critical for correctness)
    // 1. environmentAutoUpdate
    app.m_environmentAutoUpdate = cfg.environmentAutoUpdate;

    // 2. iblEnabled (master toggle before lighting)
    app.m_iblEnabled = cfg.iblEnabled;

    // 3. Renderer settings mirrored by the standalone App UI
    app.m_lightingParams = cfg.renderer.lighting;
    app.m_toneMapParams = cfg.renderer.toneMap;
    app.m_renderingPath = cfg.renderer.renderingPath;
    app.m_renderViewMode = cfg.renderer.renderViewMode;
    app.m_lightingPassDebugGradient = cfg.renderer.lightingPassDebugGradient;
    app.m_backBufferClearColor = cfg.renderer.backBufferClearColor;

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

    // 5. Apply the shared renderer settings after environment resource changes.
    app.m_sceneRenderer.ApplySettings(cfg.renderer);
}

// ---------------------------------------------------------------------------
// Load / Save / Reset orchestration
// ---------------------------------------------------------------------------

void SceneConfigManager::LoadAndApplyForScene(int sceneIndex,
                                              RtPbrSurveyApp& app,
                                              RtPbrSurveyEngine& engine,
                                              const Engine::SampleScene& scene)
{
    ReadDefaultsFromDisk();
    ReadUserConfigFromDisk();

    const std::string name = SceneConfigKey(scene, sceneIndex);
    SceneConfig defaults = CaptureFromApp(app, engine, scene);
    defaults.sceneName = name;

    auto defaultEntry = FindEntryByIndex(m_defaults, scene, sceneIndex);
    auto userEntry = FindEntryByIndex(m_userOverrides, scene, sceneIndex);

    SceneConfig merged = Merge(defaultEntry.value_or(defaults), userEntry);

    ApplyToEngine(merged, app, engine);
}

void SceneConfigManager::SaveCurrentScene(int sceneIndex,
                                          const RtPbrSurveyApp& app,
                                          const RtPbrSurveyEngine& engine,
                                          const Engine::SampleScene& scene)
{
    ReadUserConfigFromDisk();

    const std::string name = SceneConfigKey(scene, sceneIndex);
    m_userOverrides[name] = CaptureFromApp(app, engine, scene);
    m_userOverrides[name].sceneName = name;

    WriteUserConfigToDisk();
}

void SceneConfigManager::SaveAsDefault(int sceneIndex,
                                       RtPbrSurveyApp& app,
                                       RtPbrSurveyEngine& engine,
                                       const Engine::SampleScene& scene)
{
    const SceneConfig cfg = CaptureFromApp(app, engine, scene);
    const std::string name = SceneConfigKey(scene, sceneIndex);

    // Read current defaults file
    std::ifstream inFile(m_defaultsPath);
    json root;
    root["version"] = 1;
    root["scenes"] = json::object();
    if (inFile.is_open())
    {
        try
        {
            inFile >> root;
        }
        catch (...)
        {
        }
    }

    // Update the entry for this scene
    root["scenes"][name] = SceneConfigToJson(cfg);

    // Write back
    std::ofstream outFile(m_defaultsPath);
    if (outFile.is_open())
    {
        outFile << root.dump(2);
        outFile.close();
    }

    // Refresh in-memory defaults
    ReadDefaultsFromDisk();
}

void SceneConfigManager::LoadDefaultsForScene(int sceneIndex,
                                              RtPbrSurveyApp& app,
                                              RtPbrSurveyEngine& engine,
                                              const Engine::SampleScene& scene)
{
    ReadDefaultsFromDisk();

    auto defaultEntry = FindEntryByIndex(m_defaults, scene, sceneIndex);
    if (!defaultEntry.has_value())
    {
        return;
    }

    ApplyToEngine(defaultEntry.value(), app, engine);
}

void SceneConfigManager::ResetCurrentScene(int sceneIndex,
                                           RtPbrSurveyApp& app,
                                           RtPbrSurveyEngine& engine,
                                           const Engine::SampleScene& scene)
{
    ReadUserConfigFromDisk();
    ReadDefaultsFromDisk();

    const std::string name = SceneConfigKey(scene, sceneIndex);
    m_userOverrides.erase(name);
    WriteUserConfigToDisk();

    auto defaultEntry = FindEntryByIndex(m_defaults, scene, sceneIndex);
    if (!defaultEntry.has_value())
    {
        return;
    }

    ApplyToEngine(defaultEntry.value(), app, engine);
}

void SceneConfigManager::ResetAllScenes(RtPbrSurveyApp& app,
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

ConfigSource SceneConfigManager::ActiveSourceForScene(const Engine::SampleScene& scene, int sceneIndex) const
{
    const std::string key = SceneConfigKey(scene, sceneIndex);
    ConfigSource source = ActiveSource(key);
    if (source != ConfigSource::CodeDefaults)
    {
        return source;
    }

    if (dynamic_cast<const Engine::AnimatedShadowGridScene*>(&scene) != nullptr)
    {
        return ActiveSource("Animated Shadow Grid");
    }
    if (dynamic_cast<const Engine::ContactShadowTestScene*>(&scene) != nullptr)
    {
        return ActiveSource("Contact Shadow Test");
    }
    if (dynamic_cast<const Engine::OccluderWallTestScene*>(&scene) != nullptr)
    {
        return ActiveSource("Occluder Wall Test");
    }

    return source;
}

} // namespace App
