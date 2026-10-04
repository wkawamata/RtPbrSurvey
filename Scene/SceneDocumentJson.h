#pragma once

#include "Scene/SceneDocument.h"

#include <filesystem>
#include <string>
#include <string_view>

namespace RtPbrSurvey
{

bool ValidateSceneDocument(const SceneDocument& document, std::string* error = nullptr);

bool SerializeSceneDocument(const SceneDocument& document,
                            std::string& jsonText,
                            std::string* error = nullptr,
                            int indent = 2);

bool DeserializeSceneDocument(std::string_view jsonText, SceneDocument& document, std::string* error = nullptr);

bool LoadSceneDocumentFile(const std::string& path, SceneDocument& document, std::string* error = nullptr);
bool SaveSceneDocumentFile(const std::string& path, const SceneDocument& document, std::string* error = nullptr);

// Rewrites relative asset and render preset references for a Save As operation.
// The document is unchanged when a reference cannot be expressed relative to the new directory.
bool RebaseSceneDocumentPaths(SceneDocument& document,
                              const std::filesystem::path& oldSceneDirectory,
                              const std::filesystem::path& newSceneDirectory,
                              std::string* error = nullptr);

} // namespace RtPbrSurvey
