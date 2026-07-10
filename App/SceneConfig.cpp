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
