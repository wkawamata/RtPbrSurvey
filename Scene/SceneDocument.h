#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace RtPbrSurvey
{

struct SceneFloat3
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct SceneFloat4
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 0.0f;
};

struct SceneTransform
{
    SceneFloat3 translation = {0.0f, 0.0f, 0.0f};
    // Quaternion stored as x, y, z, w.
    SceneFloat4 rotation = {0.0f, 0.0f, 0.0f, 1.0f};
    SceneFloat3 scale = {1.0f, 1.0f, 1.0f};
};

enum class SceneNodeType : uint8_t
{
    Empty,
    Gltf,
    Primitive,
};

enum class ScenePrimitiveKind : uint8_t
{
    Cube,
    Sphere,
    Plane,
    Cylinder,
};

struct ScenePrimitive
{
    ScenePrimitiveKind kind = ScenePrimitiveKind::Cube;
    float size = 1.0f;
    float radius = 0.5f;
    float height = 1.0f;
    float width = 1.0f;
    float depth = 1.0f;
    uint32_t stacks = 24;
    uint32_t slices = 32;
    uint32_t radialSegments = 32;
};

struct SceneAsset
{
    std::string id;
    std::string path;
};

struct SceneMaterial
{
    std::string id;
    std::string name;
    SceneFloat4 baseColor = {1.0f, 1.0f, 1.0f, 1.0f};
    float metallic = 0.0f;
    float roughness = 0.5f;
};

struct SceneNode
{
    std::string id;
    std::string name;
    std::optional<std::string> parentId;
    SceneNodeType type = SceneNodeType::Empty;
    SceneTransform transform;
    bool visible = true;

    // Used by Gltf nodes.
    std::string assetId;

    // Used by Primitive nodes.
    ScenePrimitive primitive;
    std::string materialId;
};

enum class SceneCameraProjection : uint8_t
{
    Perspective,
    Orthographic,
};

struct SceneCamera
{
    SceneFloat3 position = {0.0f, 0.0f, -5.0f};
    SceneFloat3 target = {0.0f, 0.0f, 0.0f};
    SceneFloat3 up = {0.0f, 1.0f, 0.0f};
    SceneCameraProjection projection = SceneCameraProjection::Perspective;
    float verticalFovDegrees = 60.0f;
    float orthographicHeight = 10.0f;
    float nearZ = 0.1f;
    float farZ = 1000.0f;
};

enum class SceneEnvironmentSource : uint8_t
{
    Procedural,
};

struct SceneEnvironment
{
    SceneEnvironmentSource source = SceneEnvironmentSource::Procedural;
    bool iblEnabled = true;
    SceneFloat3 skyColor = {0.42f, 0.56f, 0.72f};
    SceneFloat3 groundColor = {0.18f, 0.17f, 0.15f};
    SceneFloat3 lightColor = {1.0f, 0.96f, 0.86f};
    SceneFloat3 lightDirection = {0.35f, 0.75f, 0.25f};
    float backgroundIntensity = 0.6f;
    float lightIntensity = 6.0f;
    float lightSize = 0.12f;
    float fillIntensity = 0.12f;
    float colorPanelIntensity = 1.5f;
    float horizonSharpness = 0.08f;
};

struct SceneDocument
{
    static constexpr int kSchemaVersion = 1;

    int schemaVersion = kSchemaVersion;
    std::string sceneId;
    std::string name;
    std::string renderPresetPath;
    std::vector<SceneAsset> assets;
    std::vector<SceneMaterial> materials;
    std::vector<SceneNode> nodes;
    SceneCamera camera;
    SceneEnvironment environment;
};

// Creates an unsaved document. File I/O, JSON validation, and ID generation belong to later layers.
inline SceneDocument CreateEmptySceneDocument(std::string sceneId, std::string name)
{
    SceneDocument document;
    document.sceneId = std::move(sceneId);
    document.name = std::move(name);
    return document;
}

// Checks the identifier collections that are local to a document. Referential and graph validation is separate.
inline bool HasUniqueSceneDocumentIds(const SceneDocument& document)
{
    std::unordered_set<std::string> ids;
    for (const SceneAsset& asset : document.assets)
    {
        if (asset.id.empty() || !ids.insert(asset.id).second)
        {
            return false;
        }
    }

    ids.clear();
    for (const SceneMaterial& material : document.materials)
    {
        if (material.id.empty() || !ids.insert(material.id).second)
        {
            return false;
        }
    }

    ids.clear();
    for (const SceneNode& node : document.nodes)
    {
        if (node.id.empty() || !ids.insert(node.id).second)
        {
            return false;
        }
    }

    return !document.sceneId.empty();
}

} // namespace RtPbrSurvey
