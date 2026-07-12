#pragma once

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
};

struct SceneLightingConfig
{
    std::array<float, 3> lightDirection = {0.0f, 1.0f, -1.0f};
    std::array<float, 3> lightColor = {1.0f, 1.0f, 1.0f};
    float iblIntensity = 0.10f;
    float diffuseIntensity = 1.0f;
    float iblDebugMip = 0.0f;
    float iblDebugExposure = 0.25f;
    bool skyboxEnabled = true;
    bool skyboxPreview = false;
    float skyboxPreviewExposure = 1.0f;
    bool directLightEnabled = true;
    bool diffuseIblEnabled = true;
    bool specularIblEnabled = true;
    bool emissiveEnabled = true;
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

struct SceneToneMapConfig
{
    int operatorIndex = 0;
    float exposure = 1.0f;
    float paperWhiteNits = 300.0f;
    float maxDisplayNits = 1000.0f;
};

struct SceneShadowConfig
{
    bool enabled = true;
    float normalBias = 0.01f;
    float rayTMin = 0.001f;
    float rayTMax = 10000.0f;
    bool softShadowEnabled = true;
    int sampleCount = 8;
    float lightAngularRadius = 0.1f;
    float jitterStrength = 2.0f;
};

struct SceneHybridReflectionConfig
{
    bool enabled = true;
    bool materialGateEnabled = false;
    float maxRoughness = 0.35f;
    float minMetallic = 0.0f;
    bool hitOverlayEnabled = false;
    int hitOverlayMode = 0;
    float hitOverlayIntensity = 0.2f;
    int hitNormalSource = 0;
    bool contributionEnabled = false;
    float contributionIntensity = 0.25f;
};

struct SceneSpecularDebugLineConfig
{
    bool enabled = true;
    float lineLength = 1.0f;
    bool showViewRay = true;
    bool showNormal = true;
    bool showReflection = true;
};

struct SceneConfig
{
    std::string sceneName;

    SceneCameraConfig camera;
    SceneLightingConfig lighting;
    SceneEnvironmentConfig environment;
    SceneToneMapConfig toneMap;
    SceneShadowConfig shadow;
    SceneHybridReflectionConfig hybridReflection;
    SceneSpecularDebugLineConfig specularDebugLines;

    // Scene-level params
    float meshScale = 0.5f;
    int displayInstanceCount = 1;
    int selectedMaterialIndex = 0;
    bool isPlaying = false;

    // Rendering / debug
    int renderingPath = 1; // 0: Forward, 1: Deferred
    int renderViewMode = 0;
    bool lightingPassDebugGradient = false;
    std::array<float, 4> backBufferClearColor = {0.0f, 0.2f, 0.4f, 1.0f};
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
        int sceneIndex) const;
};

} // namespace App
