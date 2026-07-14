#pragma once

#include "Scene.h"

#include <DirectXMath.h>
#include <cstdint>
#include <string>

namespace Engine
{

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
    void SetMesh(SceneMesh mesh);

    uint32_t AddMaterial(const SceneMaterial& material);
    uint32_t AddSolidColorMaterial(uint8_t r, uint8_t g, uint8_t b, uint8_t a);

    void AddInstance(DirectX::FXMMATRIX world, uint32_t materialId);
    void AddInstance(DirectX::FXMMATRIX world, DirectX::CXMMATRIX prevWorld, uint32_t materialId);
    void SetCamera(const CameraState& camera);

    void AppendCube(float size, uint32_t materialId);
    void AppendSphere(float radius, int stackCount, int sliceCount, uint32_t materialId);

private:
    Scene m_scene;
    SceneMesh m_mesh;
};

} // namespace Engine
