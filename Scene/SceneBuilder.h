#pragma once

#include "Scene.h"

#include <DirectXMath.h>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace Engine
{

struct GltfNodeMeshAddResult
{
    GltfNodeMeshStatus status = GltfNodeMeshStatus::InvalidAsset;
    std::optional<SceneMeshId> meshId;
    std::string message;

    explicit operator bool() const
    {
        return status == GltfNodeMeshStatus::Success;
    }
};

class SceneBuilder
{
public:
    SceneBuilder();

    Scene& GetScene();
    const Scene& GetScene() const;
    SceneMesh& GetMesh();
    const SceneMesh& GetMesh() const;

    void Clear();
    bool LoadGltfMesh(const std::string& path);
    std::optional<SceneMeshId> AddGltfMesh(const std::string& path);
    GltfNodeMeshAddResult AddGltfNodeMesh(const GltfSceneAsset& asset, const std::string& nodeName);
    void SetMesh(SceneMesh mesh);

    uint32_t AddMaterial(const SceneMaterial& material);
    uint32_t AddSolidColorMaterial(uint8_t r, uint8_t g, uint8_t b, uint8_t a);
    uint32_t AddTextureRGBA8(uint32_t width,
                             uint32_t height,
                             std::span<const uint8_t> rgba8Pixels,
                             SceneTextureOptions options = {});
    uint32_t AddTexturedMaterial(uint32_t albedoTextureIndex,
                                 DirectX::XMFLOAT2 uvScale = {1.0f, 1.0f},
                                 DirectX::XMFLOAT2 uvOffset = {0.0f, 0.0f});

    SceneMeshId AddCube(float size);
    SceneMeshId AddSphere(float radius, int stackCount, int sliceCount);
    SceneMeshId AddCylinder(float radius,
                            float height,
                            uint32_t radialSegments,
                            CylinderCapMode capMode = CylinderCapMode::Both);

    // Accepts ordinary DirectXMath world matrices. SceneBuilder converts them to InstanceData storage layout.
    void AddInstance(DirectX::FXMMATRIX world, uint32_t materialId);
    void AddInstance(DirectX::FXMMATRIX world, DirectX::CXMMATRIX prevWorld, uint32_t materialId);
    void AddInstance(SceneMeshId meshId, DirectX::FXMMATRIX world, uint32_t materialId);
    void AddInstance(SceneMeshId meshId,
                     DirectX::FXMMATRIX world,
                     DirectX::CXMMATRIX prevWorld,
                     uint32_t materialId);
    void SetCamera(const CameraState& camera);

    void AppendCube(float size, uint32_t materialId);
    void AppendSphere(float radius, int stackCount, int sliceCount, uint32_t materialId);

private:
    std::optional<SceneMeshId> AddGltfMeshData(GltfMeshData gltfMesh);
    SceneMeshId AddMeshRange(const SceneMesh& mesh);
    SceneMeshId EnsureDefaultMeshRange();

    Scene m_scene;
    SceneMesh m_mesh;
    bool m_legacyCompositeActive = false;
};

} // namespace Engine
