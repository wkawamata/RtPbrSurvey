#pragma once

#include "Runtime/SceneRendererSettings.h"

#include <filesystem>
#include <string>

namespace App
{

struct LoadedRenderPreset
{
    std::filesystem::path path;
    RtPbrSurvey::SceneRendererSettings settings;
};

bool LoadRenderPresetFile(const std::filesystem::path& path,
                          const RtPbrSurvey::SceneRendererSettings& defaults,
                          LoadedRenderPreset& preset,
                          std::string* error = nullptr);

bool SaveRenderPresetFile(const std::filesystem::path& path,
                          const RtPbrSurvey::SceneRendererSettings& settings,
                          std::string* error = nullptr);

bool ResolveRenderPresetPath(const std::filesystem::path& sceneDocumentPath,
                             const std::string& scenePresetPath,
                             const std::filesystem::path& overridePath,
                             std::filesystem::path& path,
                             std::string* error = nullptr);

} // namespace App
