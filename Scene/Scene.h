#pragma once

#include "../GltfLoader.h"

#include <cstdint>
#include <vector>

namespace Engine
{

using SceneMeshId = uint32_t;

enum class CylinderCapMode
{
    None,
    Both,
};

enum class CameraProjection
{
    Perspective,
    Orthographic,
};

struct CameraState
{
    DirectX::XMFLOAT3 pos = {0.0f, 0.0f, -5.0f};
    DirectX::XMFLOAT3 rot = {0.0f, 0.0f, 0.0f};
    DirectX::XMFLOAT3 gazePoint = {0.0f, 0.0f, 0.0f};
    DirectX::XMFLOAT3 up = {0.0f, 1.0f, 0.0f};
    CameraProjection projection = CameraProjection::Perspective;
    // Vertical field of view in degrees. Kept as "fov" for source compatibility.
    float fov = 60.0f;
    float orthographicHeight = 10.0f;
    float nearZ = 0.1f;
    float farZ = 10000.0f;
};

struct alignas(16) InstanceData
{
    DirectX::XMFLOAT4X4 world;
    DirectX::XMFLOAT4X4 prevWorld;
    uint32_t materialId;
    SceneMeshId meshId;
};

using SceneVertex = GltfVertex;

enum class TextureColorSpace
{
    Linear,
    Srgb,
};

struct SceneTextureOptions
{
    bool generateMipmaps = true;
    TextureColorSpace colorSpace = TextureColorSpace::Srgb;
};

struct SceneTexture
{
    int width = 0;
    int height = 0;
    int component = 0;
    bool generateMipmaps = false;
    TextureColorSpace colorSpace = TextureColorSpace::Srgb;
    std::vector<unsigned char> pixels;
};

struct SceneMaterial
{
    int albedoTexIndex = -1;
    int metallicRoughnessTexIndex = -1;
    int emissiveTexIndex = -1;
    int occlusionTexIndex = -1;
    int normalTexIndex = -1;
    float roughnessFactor = 1.0f;
    float metallicFactor = 1.0f;
    float occlusionStrength = 1.0f;
    float ambientOcclusionFactor = 1.0f;
    float emissiveScale = 1.0f;
    DirectX::XMFLOAT2 uvScale = {1.0f, 1.0f};
    DirectX::XMFLOAT2 uvOffset = {0.0f, 0.0f};
};

struct SceneMesh
{
    struct Range
    {
        uint32_t firstVertex = 0;
        uint32_t vertexCount = 0;
        uint32_t firstIndex = 0;
        uint32_t indexCount = 0;
    };

    std::vector<SceneVertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<Range> ranges;
    std::vector<SceneMaterial> materials;
    int materialIndex = 0;
    std::vector<SceneTexture> textures;
};

class Scene
{
public:
    CameraState camera;
    std::vector<InstanceData> instances;
    const SceneMesh* mesh = nullptr;
};

} // namespace Engine
