#include "stdafx.h"

#include "SceneBuilder.h"

#include "ProceduralSceneBuilder.h"
#include "../GltfLoader.h"

#include <DirectXMath.h>
#include <DirectXMathMatrix.inl>
#include <utility>

namespace Engine
{

SceneBuilder::SceneBuilder()
{
    Clear();
}

Scene& SceneBuilder::GetScene()
{
    return m_scene;
}

const Scene& SceneBuilder::GetScene() const
{
    return m_scene;
}

SceneMesh& SceneBuilder::GetMesh()
{
    return m_mesh;
}

const SceneMesh& SceneBuilder::GetMesh() const
{
    return m_mesh;
}

void SceneBuilder::Clear()
{
    m_scene = {};
    m_mesh = {};
    m_scene.mesh = &m_mesh;
}

bool SceneBuilder::LoadGltfMesh(const std::string& path)
{
    GltfMeshData gltfMesh = {};
    if (!::LoadGltfMesh(path, gltfMesh))
    {
        return false;
    }

    m_mesh.vertices = std::move(gltfMesh.vertices);
    m_mesh.indices = std::move(gltfMesh.indices);
    m_mesh.materialIndex = gltfMesh.materialIndex;

    m_mesh.materials.clear();
    m_mesh.materials.reserve(gltfMesh.materials.size());
    for (const GltfMaterial& gltfMaterial : gltfMesh.materials)
    {
        SceneMaterial material = {};
        material.albedoTexIndex = gltfMaterial.albedoTexIndex;
        material.metallicRoughnessTexIndex = gltfMaterial.metallicRoughnessTexIndex;
        material.emissiveTexIndex = gltfMaterial.emissiveTexIndex;
        material.occlusionTexIndex = gltfMaterial.occlusionTexIndex;
        material.normalTexIndex = gltfMaterial.normalTexIndex;
        material.roughnessFactor = gltfMaterial.roughnessFactor;
        material.metallicFactor = gltfMaterial.metallicFactor;
        material.occlusionStrength = gltfMaterial.occlusionStrength;
        m_mesh.materials.push_back(material);
    }

    m_mesh.textures.clear();
    m_mesh.textures.reserve(gltfMesh.textures.size());
    for (GltfTextureData& gltfTexture : gltfMesh.textures)
    {
        SceneTexture texture = {};
        texture.width = gltfTexture.width;
        texture.height = gltfTexture.height;
        texture.component = gltfTexture.component;
        texture.pixels = std::move(gltfTexture.pixels);
        m_mesh.textures.push_back(std::move(texture));
    }

    m_scene.mesh = &m_mesh;
    return true;
}

void SceneBuilder::SetMesh(SceneMesh mesh)
{
    m_mesh = std::move(mesh);
    m_scene.mesh = &m_mesh;
}

uint32_t SceneBuilder::AddMaterial(const SceneMaterial& material)
{
    m_mesh.materials.push_back(material);
    return static_cast<uint32_t>(m_mesh.materials.size() - 1);
}

uint32_t SceneBuilder::AddSolidColorMaterial(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    const int textureIndex = Procedural::AddSolidColorTexture(m_mesh, r, g, b, a);

    SceneMaterial material = {};
    material.albedoTexIndex = textureIndex;
    material.metallicRoughnessTexIndex = textureIndex;
    material.occlusionTexIndex = textureIndex;
    material.emissiveTexIndex = textureIndex;
    material.normalTexIndex = -1;
    return AddMaterial(material);
}

void SceneBuilder::AddInstance(DirectX::FXMMATRIX world, uint32_t materialId)
{
    AddInstance(world, world, materialId);
}

void SceneBuilder::AddInstance(DirectX::FXMMATRIX world, DirectX::CXMMATRIX prevWorld, uint32_t materialId)
{
    InstanceData instance = {};
    DirectX::XMStoreFloat4x4(&instance.world, DirectX::XMMatrixTranspose(world));
    DirectX::XMStoreFloat4x4(&instance.prevWorld, DirectX::XMMatrixTranspose(prevWorld));
    instance.materialId = materialId;
    m_scene.instances.push_back(instance);
}

void SceneBuilder::SetCamera(const CameraState& camera)
{
    m_scene.camera = camera;
}

void SceneBuilder::AppendCube(float size, uint32_t materialId)
{
    Procedural::AppendMesh(m_mesh, Procedural::CreateCubeMesh(size), materialId);
}

void SceneBuilder::AppendSphere(float radius, int stackCount, int sliceCount, uint32_t materialId)
{
    Procedural::AppendMesh(m_mesh, Procedural::CreateSphereMesh(radius, stackCount, sliceCount), materialId);
}

} // namespace Engine
