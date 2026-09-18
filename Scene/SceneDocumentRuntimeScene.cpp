#include "stdafx.h"

#include "Scene/SceneDocumentRuntimeScene.h"

#include "Scene/SceneDocumentJson.h"

#include <algorithm>
#include <stdexcept>

namespace Engine
{

SceneDocumentRuntimeScene::SceneDocumentRuntimeScene(std::filesystem::path documentPath)
    : m_documentPath(std::move(documentPath))
{
}

bool SceneDocumentRuntimeScene::LoadFromFile(std::string* error)
{
    RtPbrSurvey::SceneDocument document;
    if (!RtPbrSurvey::LoadSceneDocumentFile(m_documentPath.string(), document, error))
    {
        return false;
    }
    return LoadFromDocument(document, m_documentPath.parent_path(), error);
}

bool SceneDocumentRuntimeScene::LoadFromDocument(const RtPbrSurvey::SceneDocument& document,
                                                  const std::filesystem::path& sceneDirectory,
                                                  std::string* error)
{
    if (!m_builder.Build(document, sceneDirectory, error))
    {
        return false;
    }

    m_document = document;
    m_name = m_document.name;
    m_displayInstanceCount = static_cast<int>(m_builder.Builder().GetScene().instances.size());
    if (error != nullptr)
    {
        error->clear();
    }
    return true;
}

const std::filesystem::path& SceneDocumentRuntimeScene::DocumentPath() const
{
    return m_documentPath;
}

const RtPbrSurvey::SceneDocument& SceneDocumentRuntimeScene::Document() const
{
    return m_document;
}

std::optional<std::string> SceneDocumentRuntimeScene::FindNodeIdByInstanceIndex(size_t instanceIndex) const
{
    for (const auto& [nodeId, nodeInstanceIndex] : m_builder.Result().nodeInstanceIndices)
    {
        if (nodeInstanceIndex == instanceIndex)
        {
            return nodeId;
        }
    }
    return std::nullopt;
}

const char* SceneDocumentRuntimeScene::Name() const
{
    return m_name.c_str();
}

void SceneDocumentRuntimeScene::Load()
{
    std::string error;
    if (!LoadFromFile(&error))
    {
        throw std::runtime_error(error);
    }
}

void SceneDocumentRuntimeScene::Reset()
{
}

void SceneDocumentRuntimeScene::Update(float, const SampleSceneUpdateContext&)
{
}

Scene& SceneDocumentRuntimeScene::GetScene()
{
    return m_builder.Builder().GetScene();
}

const Scene& SceneDocumentRuntimeScene::GetScene() const
{
    return m_builder.Builder().GetScene();
}

SceneMesh& SceneDocumentRuntimeScene::GetMesh()
{
    return m_builder.Builder().GetMesh();
}

const SceneMesh& SceneDocumentRuntimeScene::GetMesh() const
{
    return m_builder.Builder().GetMesh();
}

int SceneDocumentRuntimeScene::DisplayInstanceCount() const
{
    return m_displayInstanceCount;
}

int SceneDocumentRuntimeScene::MaxDisplayInstanceCount() const
{
    return static_cast<int>(m_builder.Builder().GetScene().instances.size());
}

void SceneDocumentRuntimeScene::SetDisplayInstanceCount(int count)
{
    m_displayInstanceCount = std::clamp(count, 0, MaxDisplayInstanceCount());
}

float SceneDocumentRuntimeScene::DefaultMeshScale() const
{
    return 1.0f;
}

} // namespace Engine
