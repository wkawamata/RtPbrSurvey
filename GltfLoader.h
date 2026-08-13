// GltfLoader.h
#pragma once

#include <DirectXMath.h>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

static constexpr uint32_t kGltfVertexMaterialFromInstance = 0xffffffffu;

struct GltfVertex
{
    DirectX::XMFLOAT3 position;
    DirectX::XMFLOAT2 uv;
    DirectX::XMFLOAT3 normal;
    DirectX::XMFLOAT4 tangent = {0.0f, 0.0f, 0.0f, 0.0f};
    uint32_t materialId = kGltfVertexMaterialFromInstance;
};

struct GltfMaterial
{
    int albedoTexIndex = -1;
    int metallicRoughnessTexIndex = -1;
    int emissiveTexIndex = -1;
    int occlusionTexIndex = -1;
    int normalTexIndex = -1;
    float baseColorFactor[4] = {1, 1, 1, 1};
    float roughnessFactor = 1.0f;
    float metallicFactor = 1.0f;
    float occlusionStrength = 1.0f;
};

struct GltfTextureData
{
    int width = 0;
    int height = 0;
    int component = 0;
    std::vector<unsigned char> pixels; // RGBA8
};

struct GltfMeshData
{
    std::vector<GltfVertex> vertices;
    std::vector<uint32_t> indices;

    std::vector<GltfMaterial> materials; // interim
    int materialIndex = 0;

    std::vector<GltfTextureData> textures; // interim
};

bool LoadGltfMesh(const std::string& path, GltfMeshData& outMesh);

namespace Engine
{

struct GltfSceneAssetLoadResult;
class SceneBuilder;

enum class GltfNodeMeshStatus
{
    Success,
    InvalidAsset,
    NodeNotFound,
    DuplicateNodeName,
    MeshConversionFailed,
};

class GltfSceneAsset
{
public:
    GltfSceneAsset() = default;

    bool IsValid() const;

private:
    struct Impl;
    explicit GltfSceneAsset(std::shared_ptr<const Impl> impl);
    GltfNodeMeshStatus ExtractNodeMesh(const std::string& nodeName, GltfMeshData& outMesh, std::string& message) const;

    std::shared_ptr<const Impl> m_impl;

    friend GltfSceneAssetLoadResult LoadGltfSceneAsset(const std::string& path);
    friend std::vector<std::string> GetGltfMeshNodeNames(const GltfSceneAsset& asset);
    friend class SceneBuilder;
};

enum class GltfSceneAssetLoadStatus
{
    Success,
    FileLoadFailed,
    NoMeshes,
};

struct GltfSceneAssetLoadResult
{
    GltfSceneAssetLoadStatus status = GltfSceneAssetLoadStatus::FileLoadFailed;
    GltfSceneAsset asset;
    std::string message;

    explicit operator bool() const
    {
        return status == GltfSceneAssetLoadStatus::Success;
    }
};

GltfSceneAssetLoadResult LoadGltfSceneAsset(const std::string& path);
std::vector<std::string> GetGltfMeshNodeNames(const GltfSceneAsset& asset);

} // namespace Engine
