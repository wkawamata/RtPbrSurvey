#pragma once

#include "Scene/SceneDocument.h"
#include "Scene/SceneGraph.h"

#include "Scene/SceneBuilder.h"

#include <cstddef>
#include <filesystem>
#include <string>
#include <unordered_map>

namespace RtPbrSurvey
{

struct SceneDocumentBuildResult
{
    SceneGraphEvaluation graph;
    std::unordered_map<std::string, size_t> nodeInstanceIndices;
    std::unordered_map<std::string, uint32_t> materialIds;
};

class SceneDocumentBuilder
{
public:
    bool Build(const SceneDocument& document, std::string* error = nullptr);
    bool Build(const SceneDocument& document,
               const std::filesystem::path& sceneDirectory,
               std::string* error = nullptr);

    Engine::SceneBuilder& Builder()
    {
        return m_builder;
    }
    const Engine::SceneBuilder& Builder() const
    {
        return m_builder;
    }
    const SceneDocumentBuildResult& Result() const
    {
        return m_result;
    }

private:
    Engine::SceneBuilder m_builder;
    SceneDocumentBuildResult m_result;
};

} // namespace RtPbrSurvey
