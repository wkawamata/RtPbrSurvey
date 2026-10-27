#pragma once

#include <algorithm>
#include <filesystem>
#include <vector>

namespace App
{

inline std::vector<std::filesystem::path> FindSavedSceneFiles(const std::filesystem::path& assetsRoot)
{
    const std::filesystem::path sceneRoots[] = {
        assetsRoot / "Scenes", assetsRoot / "Scene" / "PathTracingValidation"};
    std::vector<std::filesystem::path> sceneFiles;
    for (const std::filesystem::path& sceneRoot : sceneRoots)
    {
        std::error_code error;
        if (!std::filesystem::is_directory(sceneRoot, error))
        {
            continue;
        }
        const std::filesystem::directory_iterator end;
        for (std::filesystem::directory_iterator iterator(sceneRoot, error);
             !error && iterator != end; iterator.increment(error))
        {
            const std::filesystem::directory_entry& entry = *iterator;
            std::error_code entryError;
            if (!entry.is_directory(entryError))
            {
                continue;
            }
            const std::filesystem::path scenePath = entry.path() / "scene.json";
            if (std::filesystem::is_regular_file(scenePath, entryError))
            {
                sceneFiles.push_back(scenePath);
            }
        }
    }
    std::sort(sceneFiles.begin(), sceneFiles.end());
    return sceneFiles;
}
} // namespace App
