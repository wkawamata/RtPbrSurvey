#pragma once

#include "Scene/SceneDocument.h"
#include "Scene/SceneDocumentBuilder.h"
#include "Scene/SampleScene.h"

#include <filesystem>
#include <optional>
#include <string>

namespace Engine
{

class SceneDocumentRuntimeScene final : public SampleScene
{
public:
    explicit SceneDocumentRuntimeScene(std::filesystem::path documentPath);

    bool LoadFromFile(std::string* error = nullptr);
    bool LoadFromDocument(const RtPbrSurvey::SceneDocument& document,
                          const std::filesystem::path& sceneDirectory,
                          std::string* error = nullptr);
    const std::filesystem::path& DocumentPath() const;
    const RtPbrSurvey::SceneDocument& Document() const;
    std::optional<std::string> FindNodeIdByInstanceIndex(size_t instanceIndex) const;

    const char* Name() const override;
    void Load() override;
    void Reset() override;
    void Update(float deltaTime, const SampleSceneUpdateContext& context) override;
    Scene& GetScene() override;
    const Scene& GetScene() const override;
    SceneMesh& GetMesh() override;
    const SceneMesh& GetMesh() const override;
    int DisplayInstanceCount() const override;
    int MaxDisplayInstanceCount() const override;
    void SetDisplayInstanceCount(int count) override;
    float DefaultMeshScale() const override;

private:
    std::filesystem::path m_documentPath;
    RtPbrSurvey::SceneDocument m_document;
    RtPbrSurvey::SceneDocumentBuilder m_builder;
    std::string m_name;
    int m_displayInstanceCount = 0;
};

} // namespace Engine
