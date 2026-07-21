#pragma once

#include "Runtime/SceneRendererSettings.h"
#include "Scene/SampleScene.h"

#include <array>
#include <optional>
#include <string>
#include <unordered_map>

class RtPbrSurveyApp;
class RtPbrSurveyEngine;

namespace App
{

struct SceneCameraConfig
{
    // "arcball" or "freelook"
    std::string mode = "freelook";

    // Arcball fields
    float yaw = 0.0f;
    float pitch = 0.0f;
    float distance = 5.0f;
    std::array<float, 3> pivot = {0.0f, 0.0f, 0.0f};

    // FreeLook fields
    std::array<float, 3> position = {0.0f, 0.0f, -5.0f};
    std::array<float, 3> rotation = {0.0f, 0.0f, 0.0f};

    // Common
    float fov = 60.0f;
    float speedMultiplier = 1.0f;
    float nearZ = 0.1f;
    float farZ = 10000.0f;
};

struct SceneEnvironmentConfig
{
    int source = 0; // EnvironmentSource enum
    std::array<float, 3> skyColor = {0.42f, 0.56f, 0.72f};
    std::array<float, 3> groundColor = {0.18f, 0.17f, 0.15f};
    std::array<float, 3> lightColor = {1.0f, 0.96f, 0.86f};
    std::array<float, 3> lightDirection = {0.35f, 0.75f, 0.25f};
    float backgroundIntensity = 0.6f;
    float lightIntensity = 6.0f;
    float lightSize = 0.12f;
    float fillIntensity = 0.12f;
    float colorPanelIntensity = 1.5f;
    float horizonSharpness = 0.08f;
};

struct SceneConfig
{
    std::string sceneName;

    SceneCameraConfig camera;
    SceneEnvironmentConfig environment;
    RtPbrSurvey::SceneRendererSettings renderer;

    // Scene-level params
    float meshScale = 0.5f;
    int displayInstanceCount = 1;
    int selectedMaterialIndex = 0;
    bool isPlaying = false;

    // Rendering / debug
    bool iblEnabled = true;
    bool environmentAutoUpdate = true;
};

enum class ConfigSource
{
    CodeDefaults,
    DefaultFile,
    UserFile,
};

class SceneConfigManager
{
public:
    SceneConfigManager();

    void SetPaths(const std::string& defaultsPath, const std::string& userConfigPath);

    // Loads config for a scene applying cascade: code defaults -> default file -> user file
    void LoadAndApplyForScene(int sceneIndex,
                              RtPbrSurveyApp& app,
                              RtPbrSurveyEngine& engine,
                              const Engine::SampleScene& scene);

    // Saves current scene state to user config file
    void SaveCurrentScene(int sceneIndex,
                          const RtPbrSurveyApp& app,
                          const RtPbrSurveyEngine& engine,
                          const Engine::SampleScene& scene);

    // Saves current scene state as the default (overwrites defaults file entry)
    void SaveAsDefault(int sceneIndex,
                       RtPbrSurveyApp& app,
                       RtPbrSurveyEngine& engine,
                       const Engine::SampleScene& scene);

    // Re-reads default file and applies (user file on disk untouched)
    void LoadDefaultsForScene(int sceneIndex,
                              RtPbrSurveyApp& app,
                              RtPbrSurveyEngine& engine,
                              const Engine::SampleScene& scene);

    // Removes scene entry from user config, rewrites file, applies defaults
    void ResetCurrentScene(int sceneIndex,
                           RtPbrSurveyApp& app,
                           RtPbrSurveyEngine& engine,
                           const Engine::SampleScene& scene);

    // Deletes entire user config file, applies defaults for current scene
    void ResetAllScenes(RtPbrSurveyApp& app,
                        RtPbrSurveyEngine& engine,
                        const Engine::SampleScene& currentScene);

    // Returns the source hint for UI display
    ConfigSource ActiveSource(const std::string& sceneName) const;
    ConfigSource ActiveSourceForScene(const Engine::SampleScene& scene, int sceneIndex) const;

    const std::string& UserConfigPath() const { return m_userConfigPath; }

private:
    std::string m_defaultsPath;
    std::string m_userConfigPath;

    std::unordered_map<std::string, SceneConfig> m_defaults;
    std::unordered_map<std::string, SceneConfig> m_userOverrides;

    void ReadDefaultsFromDisk();
    void ReadUserConfigFromDisk();
    void WriteUserConfigToDisk();

    SceneConfig CaptureFromApp(const RtPbrSurveyApp& app,
                               const RtPbrSurveyEngine& engine,
                               const Engine::SampleScene& scene);

    void ApplyToEngine(const SceneConfig& cfg,
                       RtPbrSurveyApp& app,
                       RtPbrSurveyEngine& engine);

    SceneConfig Merge(const SceneConfig& defaults,
                      const std::optional<SceneConfig>& overrides);

    std::optional<SceneConfig> FindEntry(
        const std::unordered_map<std::string, SceneConfig>& configs,
        const std::string& sceneName) const;

    std::optional<SceneConfig> FindEntryByIndex(
        const std::unordered_map<std::string, SceneConfig>& configs,
        const Engine::SampleScene& scene,
        int sceneIndex) const;

    std::string SceneConfigKey(const Engine::SampleScene& scene, int sceneIndex) const;
};

} // namespace App
