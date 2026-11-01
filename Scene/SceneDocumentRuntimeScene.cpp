#include "stdafx.h"

#include "Scene/SceneDocumentRuntimeScene.h"

#include "Scene/SceneDocumentJson.h"

#include <algorithm>
#include <stdexcept>
#include <cmath>

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
    m_turntableInstance.reset();
    m_turntableAngle = 0.0f;
    if (document.turntable)
    {
        const auto instance = m_builder.Result().nodeInstanceIndices.find(document.turntable->nodeId);
        if (instance == m_builder.Result().nodeInstanceIndices.end())
        {
            if (error != nullptr)
            {
                *error = "Turntable node has no runtime instance.";
            }
            return false;
        }
        m_turntableInstance = instance->second;
        m_turntableBaseWorld = GetScene().instances[*m_turntableInstance].world;
    }
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
    m_turntableAngle = 0.0f;
    if (m_turntableInstance)
    {
        auto& instance = GetScene().instances[*m_turntableInstance];
        instance.world = m_turntableBaseWorld;
        instance.prevWorld = instance.world;
    }
}

void SceneDocumentRuntimeScene::Update(float deltaTime, const SampleSceneUpdateContext& context)
{
    if (!m_turntableInstance || !m_document.turntable)
    {
        return;
    }
    auto& instance = GetScene().instances[*m_turntableInstance];
    instance.prevWorld = instance.world;
    if (!context.isPlaying || !std::isfinite(deltaTime) || deltaTime <= 0.0f)
    {
        return;
    }
    const auto& animation = *m_document.turntable;
    m_turntableAngle = std::fmod(m_turntableAngle + deltaTime * animation.degreesPerSecond, 360.0f);
    const auto& pivot = animation.pivot;
    const DirectX::XMMATRIX world = DirectX::XMMatrixTranspose(DirectX::XMLoadFloat4x4(&m_turntableBaseWorld)) *
        DirectX::XMMatrixTranslation(-pivot.x, -pivot.y, -pivot.z) *
        DirectX::XMMatrixRotationY(DirectX::XMConvertToRadians(m_turntableAngle)) *
        DirectX::XMMatrixTranslation(pivot.x, pivot.y, pivot.z);
    DirectX::XMStoreFloat4x4(&instance.world, DirectX::XMMatrixTranspose(world));
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
