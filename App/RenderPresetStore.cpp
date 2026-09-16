#include "stdafx.h"

#include "App/RenderPresetStore.h"

#include <fstream>

namespace App
{

bool LoadRenderPresetFile(const std::filesystem::path& path,
                          const RtPbrSurvey::SceneRendererSettings& defaults,
                          LoadedRenderPreset& preset,
                          std::string* error)
{
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open())
    {
        if (error != nullptr)
        {
            *error = "Could not open render preset: " + path.generic_string();
        }
        return false;
    }

    const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    RtPbrSurvey::SceneRendererSettings parsed = defaults;
    if (!RtPbrSurvey::DeserializeSceneRendererSettings(text, parsed, error))
    {
        return false;
    }

    preset.path = path;
    preset.settings = std::move(parsed);
    if (error != nullptr)
    {
        error->clear();
    }
    return true;
}

bool SaveRenderPresetFile(const std::filesystem::path& path,
                          const RtPbrSurvey::SceneRendererSettings& settings,
                          std::string* error)
{
    if (path.empty())
    {
        if (error != nullptr)
        {
            *error = "Render preset path is empty.";
        }
        return false;
    }

    const std::filesystem::path temporaryPath = path.string() + ".tmp";
    std::ofstream file(temporaryPath, std::ios::binary | std::ios::trunc);
    if (!file.is_open())
    {
        if (error != nullptr)
        {
            *error = "Could not open temporary render preset: " + temporaryPath.generic_string();
        }
        return false;
    }
    file << RtPbrSurvey::SerializeSceneRendererSettings(settings);
    file.close();
    if (!file)
    {
        std::error_code filesystemError;
        std::filesystem::remove(temporaryPath, filesystemError);
        if (error != nullptr)
        {
            *error = "Could not write render preset: " + temporaryPath.generic_string();
        }
        return false;
    }

    if (!MoveFileExW(temporaryPath.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING))
    {
        std::error_code filesystemError;
        std::filesystem::remove(temporaryPath, filesystemError);
        if (error != nullptr)
        {
            *error = "Could not replace render preset: " + path.generic_string();
        }
        return false;
    }
    if (error != nullptr)
    {
        error->clear();
    }
    return true;
}

bool ResolveRenderPresetPath(const std::filesystem::path& sceneDocumentPath,
                             const std::string& scenePresetPath,
                             const std::filesystem::path& overridePath,
                             std::filesystem::path& path,
                             std::string* error)
{
    if (!overridePath.empty())
    {
        path = overridePath.is_absolute() ? overridePath : std::filesystem::absolute(overridePath);
    }
    else
    {
        const std::filesystem::path relativePath(scenePresetPath);
        if (relativePath.empty() || relativePath.is_absolute())
        {
            if (error != nullptr)
            {
                *error = "Scene renderPreset must use a non-empty relative path.";
            }
            return false;
        }
        path = sceneDocumentPath.parent_path() / relativePath;
    }

    path = path.lexically_normal();
    std::error_code filesystemError;
    if (!std::filesystem::is_regular_file(path, filesystemError))
    {
        if (error != nullptr)
        {
            *error = "Render preset was not found: " + path.generic_string();
        }
        return false;
    }
    return true;
}

} // namespace App
