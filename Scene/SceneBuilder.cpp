#include "stdafx.h"

#include "SceneBuilder.h"

#include "ProceduralSceneBuilder.h"
#include "../GltfLoader.h"

#include <DirectXMath.h>
#include <DirectXMathMatrix.inl>
#include <limits>
#include <stdexcept>
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

uint32_t SceneBuilder::AddTextureRGBA8(uint32_t width,
                                       uint32_t height,
                                       std::span<const uint8_t> rgba8Pixels,
                                       SceneTextureOptions options)
{
    if (width == 0 || height == 0 || width > static_cast<uint32_t>((std::numeric_limits<int>::max)()) ||
        height > static_cast<uint32_t>((std::numeric_limits<int>::max)()))
    {
        throw std::invalid_argument("RGBA8 texture dimensions must be positive signed 32-bit values.");
    }

    constexpr size_t componentCount = 4;
    if (static_cast<size_t>(width) > (std::numeric_limits<size_t>::max)() / static_cast<size_t>(height) ||
        static_cast<size_t>(width) * static_cast<size_t>(height) >
            (std::numeric_limits<size_t>::max)() / componentCount)
    {
        throw std::invalid_argument("RGBA8 texture dimensions overflow the pixel buffer size.");
    }
    const size_t expectedSize = static_cast<size_t>(width) * static_cast<size_t>(height) * componentCount;
    if (rgba8Pixels.size() != expectedSize)
    {
        throw std::invalid_argument("RGBA8 texture pixel count does not match width * height * 4.");
    }

    SceneTexture texture = {};
    texture.width = static_cast<int>(width);
    texture.height = static_cast<int>(height);
    texture.component = static_cast<int>(componentCount);
    texture.generateMipmaps = options.generateMipmaps;
    texture.colorSpace = options.colorSpace;
    texture.pixels.assign(rgba8Pixels.begin(), rgba8Pixels.end());
    m_mesh.textures.push_back(std::move(texture));
    return static_cast<uint32_t>(m_mesh.textures.size() - 1);
}

uint32_t SceneBuilder::AddTexturedMaterial(uint32_t albedoTextureIndex,
                                           DirectX::XMFLOAT2 uvScale,
                                           DirectX::XMFLOAT2 uvOffset)
{
    if (albedoTextureIndex >= m_mesh.textures.size())
    {
        throw std::out_of_range("Albedo texture index is outside the SceneBuilder texture list.");
    }

    SceneMaterial material = {};
    material.albedoTexIndex = static_cast<int>(albedoTextureIndex);
    material.metallicFactor = 0.0f;
    material.roughnessFactor = 1.0f;
    material.uvScale = uvScale;
    material.uvOffset = uvOffset;
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
