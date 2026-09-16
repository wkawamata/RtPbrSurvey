#include "App/RenderPresetStore.h"

#include <filesystem>
#include <fstream>
#include <iostream>

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

bool WriteTextFile(const std::filesystem::path& path, const char* text)
{
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << text;
    return static_cast<bool>(file);
}

bool TestResolveAndLoad()
{
    const std::filesystem::path directory = std::filesystem::temp_directory_path() / "RtPbrSurveyRenderPresetStoreTests";
    std::filesystem::create_directories(directory);
    const std::filesystem::path scenePath = directory / "scene.json";
    const std::filesystem::path scenePreset = directory / "scene-preset.json";
    const std::filesystem::path overridePreset = directory / "override-preset.json";
    if (!WriteTextFile(scenePreset, "{\"schemaVersion\":1,\"renderingPath\":0}") ||
        !WriteTextFile(overridePreset, "{\"schemaVersion\":1,\"renderingPath\":1,\"toneMap\":{\"exposure\":2.0}}"))
    {
        return Check(false, "test preset files are written");
    }

    std::string error;
    std::filesystem::path resolved;
    bool passed = Check(App::ResolveRenderPresetPath(scenePath, "scene-preset.json", {}, resolved, &error),
                        "scene-relative preset resolves");
    passed &= Check(resolved == scenePreset, "scene-relative preset uses the scene directory");

    App::LoadedRenderPreset loaded;
    RtPbrSurvey::SceneRendererSettings defaults;
    passed &= Check(App::LoadRenderPresetFile(resolved, defaults, loaded, &error), "scene preset loads");
    passed &= Check(loaded.settings.renderingPath == RtPbrSurveyEngine::RenderingPath::Forward,
                    "first preset changes the rendering path");

    passed &= Check(App::ResolveRenderPresetPath(scenePath, "scene-preset.json", overridePreset, resolved, &error),
                    "override preset resolves");
    passed &= Check(resolved == overridePreset, "override takes precedence over the scene reference");
    passed &= Check(App::LoadRenderPresetFile(resolved, defaults, loaded, &error), "override preset loads");
    passed &= Check(loaded.settings.renderingPath == RtPbrSurveyEngine::RenderingPath::Deferred &&
                        loaded.settings.toneMap.exposure == 2.0f,
                    "second preset provides independent settings");

    std::filesystem::remove_all(directory);
    return passed;
}

bool TestSaveAndLoad()
{
    const std::filesystem::path directory = std::filesystem::temp_directory_path() / "RtPbrSurveyRenderPresetSaveTests";
    std::filesystem::create_directories(directory);
    const std::filesystem::path presetPath = directory / "render-preset.json";

    RtPbrSurvey::SceneRendererSettings settings;
    settings.renderingPath = RtPbrSurveyEngine::RenderingPath::Forward;
    settings.toneMap.exposure = 1.25f;
    std::string error;
    bool passed = Check(App::SaveRenderPresetFile(presetPath, settings, &error), "render preset saves atomically");

    App::LoadedRenderPreset loaded;
    RtPbrSurvey::SceneRendererSettings defaults;
    passed &= Check(App::LoadRenderPresetFile(presetPath, defaults, loaded, &error), "saved render preset reloads");
    passed &= Check(loaded.settings.renderingPath == RtPbrSurveyEngine::RenderingPath::Forward &&
                        loaded.settings.toneMap.exposure == 1.25f,
                    "saved render preset preserves settings");
    passed &= Check(!std::filesystem::exists(presetPath.string() + ".tmp"), "temporary preset file is replaced");

    std::filesystem::remove_all(directory);
    return passed;
}

} // namespace

int main()
{
    if (!TestResolveAndLoad() || !TestSaveAndLoad())
    {
        return 1;
    }
    std::cout << "RenderPresetStore tests passed.\n";
    return 0;
}
