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
    m_legacyCompositeActive = false;
}

bool SceneBuilder::LoadGltfMesh(const std::string& path)
{
    GltfMeshData gltfMesh = {};
    if (!::LoadGltfMesh(path, gltfMesh))
    {
        return false;
    }

    m_mesh = {};
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
        for (int factorIndex = 0; factorIndex < 4; factorIndex++)
        {
            material.baseColorFactor[factorIndex] = gltfMaterial.baseColorFactor[factorIndex];
        }
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

    if (!m_mesh.vertices.empty())
    {
        m_mesh.ranges.push_back(
            {0,
             static_cast<uint32_t>(m_mesh.vertices.size()),
             0,
             static_cast<uint32_t>(m_mesh.indices.size())});
    }
    m_scene.mesh = &m_mesh;
    m_legacyCompositeActive = true;
    return true;
}

std::optional<SceneMeshId> SceneBuilder::AddGltfMesh(const std::string& path)
{
    GltfMeshData gltfMesh = {};
    if (!::LoadGltfMesh(path, gltfMesh))
    {
        return std::nullopt;
    }
    return AddGltfMeshData(std::move(gltfMesh));
}

GltfNodeMeshAddResult SceneBuilder::AddGltfNodeMesh(const GltfSceneAsset& asset, const std::string& nodeName)
{
    GltfNodeMeshAddResult result = {};
    GltfMeshData gltfMesh = {};
    result.status = asset.ExtractNodeMesh(nodeName, gltfMesh, result.message);
    if (result.status != GltfNodeMeshStatus::Success)
    {
        return result;
    }

    result.meshId = AddGltfMeshData(std::move(gltfMesh));
    if (!result.meshId)
    {
        result.status = GltfNodeMeshStatus::MeshConversionFailed;
        result.message = "The extracted glTF node mesh could not be added to the scene.";
    }
    return result;
}

std::optional<SceneMeshId> SceneBuilder::AddGltfMeshData(GltfMeshData gltfMesh)
{
    const uint32_t textureBase = static_cast<uint32_t>(m_mesh.textures.size());
    for (GltfTextureData& gltfTexture : gltfMesh.textures)
    {
        SceneTexture texture = {};
        texture.width = gltfTexture.width;
        texture.height = gltfTexture.height;
        texture.component = gltfTexture.component;
        texture.pixels = std::move(gltfTexture.pixels);
        m_mesh.textures.push_back(std::move(texture));
    }

    const auto remapTextureIndex = [textureBase](int textureIndex)
    {
        return textureIndex >= 0 ? static_cast<int>(textureBase) + textureIndex : -1;
    };
    const uint32_t materialBase = static_cast<uint32_t>(m_mesh.materials.size());
    for (const GltfMaterial& gltfMaterial : gltfMesh.materials)
    {
        SceneMaterial material = {};
        material.albedoTexIndex = remapTextureIndex(gltfMaterial.albedoTexIndex);
        material.metallicRoughnessTexIndex = remapTextureIndex(gltfMaterial.metallicRoughnessTexIndex);
        material.emissiveTexIndex = remapTextureIndex(gltfMaterial.emissiveTexIndex);
        material.occlusionTexIndex = remapTextureIndex(gltfMaterial.occlusionTexIndex);
        material.normalTexIndex = remapTextureIndex(gltfMaterial.normalTexIndex);
        material.roughnessFactor = gltfMaterial.roughnessFactor;
        material.metallicFactor = gltfMaterial.metallicFactor;
        material.occlusionStrength = gltfMaterial.occlusionStrength;
        for (int factorIndex = 0; factorIndex < 4; factorIndex++)
        {
            material.baseColorFactor[factorIndex] = gltfMaterial.baseColorFactor[factorIndex];
        }
        m_mesh.materials.push_back(material);
    }

    SceneMesh mesh = {};
    mesh.vertices = std::move(gltfMesh.vertices);
    mesh.indices = std::move(gltfMesh.indices);
    for (SceneVertex& vertex : mesh.vertices)
    {
        if (vertex.materialId != kGltfVertexMaterialFromInstance)
        {
            vertex.materialId += materialBase;
        }
    }

    SceneMesh::Range range = {};
    range.firstVertex = static_cast<uint32_t>(m_mesh.vertices.size());
    range.vertexCount = static_cast<uint32_t>(mesh.vertices.size());
    range.firstIndex = static_cast<uint32_t>(m_mesh.indices.size());
    range.indexCount = static_cast<uint32_t>(mesh.indices.size());
    m_mesh.vertices.insert(m_mesh.vertices.end(), mesh.vertices.begin(), mesh.vertices.end());
    for (uint32_t index : mesh.indices)
    {
        m_mesh.indices.push_back(range.firstVertex + index);
    }
    m_mesh.ranges.push_back(range);
    return static_cast<SceneMeshId>(m_mesh.ranges.size() - 1);
}

void SceneBuilder::SetMesh(SceneMesh mesh)
{
    m_mesh = std::move(mesh);
    if (m_mesh.ranges.empty() && !m_mesh.vertices.empty())
    {
        m_mesh.ranges.push_back(
            {0,
             static_cast<uint32_t>(m_mesh.vertices.size()),
             0,
             static_cast<uint32_t>(m_mesh.indices.size())});
    }
    m_scene.mesh = &m_mesh;
    m_legacyCompositeActive = !m_mesh.ranges.empty();
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

SceneMeshId SceneBuilder::AddCube(float size)
{
    return AddMeshRange(Procedural::CreateCubeMesh(size));
}

SceneMeshId SceneBuilder::AddSphere(float radius, int stackCount, int sliceCount)
{
    return AddMeshRange(Procedural::CreateSphereMesh(radius, stackCount, sliceCount));
}

SceneMeshId SceneBuilder::AddCylinder(float radius,
                                      float height,
                                      uint32_t radialSegments,
                                      CylinderCapMode capMode)
{
    return AddMeshRange(Procedural::CreateCylinderMesh(radius, height, radialSegments, capMode));
}

void SceneBuilder::AddInstance(DirectX::FXMMATRIX world, uint32_t materialId)
{
    AddInstance(EnsureDefaultMeshRange(), world, world, materialId);
}

void SceneBuilder::AddInstance(DirectX::FXMMATRIX world, DirectX::CXMMATRIX prevWorld, uint32_t materialId)
{
    AddInstance(EnsureDefaultMeshRange(), world, prevWorld, materialId);
}

void SceneBuilder::AddInstance(SceneMeshId meshId, DirectX::FXMMATRIX world, uint32_t materialId)
{
    AddInstance(meshId, world, world, materialId);
}

void SceneBuilder::AddInstance(SceneMeshId meshId,
                               DirectX::FXMMATRIX world,
                               DirectX::CXMMATRIX prevWorld,
                               uint32_t materialId)
{
    if (meshId >= m_mesh.ranges.size())
    {
        throw std::out_of_range("Scene mesh ID is outside the SceneBuilder mesh range list.");
    }
    if (materialId >= m_mesh.materials.size())
    {
        throw std::out_of_range("Material ID is outside the SceneBuilder material list.");
    }

    InstanceData instance = {};
    DirectX::XMStoreFloat4x4(&instance.world, DirectX::XMMatrixTranspose(world));
    DirectX::XMStoreFloat4x4(&instance.prevWorld, DirectX::XMMatrixTranspose(prevWorld));
    instance.materialId = materialId;
    instance.meshId = meshId;
    m_scene.instances.push_back(instance);
}

void SceneBuilder::SetCamera(const CameraState& camera)
{
    m_scene.camera = camera;
}

void SceneBuilder::AppendCube(float size, uint32_t materialId)
{
    if (!m_legacyCompositeActive)
    {
        if (!m_mesh.ranges.empty())
        {
            throw std::logic_error("AppendCube cannot extend a scene after independent mesh ranges were added.");
        }
        m_mesh.ranges.push_back(
            {static_cast<uint32_t>(m_mesh.vertices.size()), 0, static_cast<uint32_t>(m_mesh.indices.size()), 0});
        m_legacyCompositeActive = true;
    }
    if (m_mesh.ranges.size() != 1)
    {
        throw std::logic_error("AppendCube can only extend the default composite mesh range.");
    }

    const SceneMesh::Range appended = Procedural::AppendMesh(m_mesh, Procedural::CreateCubeMesh(size), materialId);
    m_mesh.ranges[0].vertexCount += appended.vertexCount;
    m_mesh.ranges[0].indexCount += appended.indexCount;
}

void SceneBuilder::AppendSphere(float radius, int stackCount, int sliceCount, uint32_t materialId)
{
    if (!m_legacyCompositeActive)
    {
        if (!m_mesh.ranges.empty())
        {
            throw std::logic_error("AppendSphere cannot extend a scene after independent mesh ranges were added.");
        }
        m_mesh.ranges.push_back(
            {static_cast<uint32_t>(m_mesh.vertices.size()), 0, static_cast<uint32_t>(m_mesh.indices.size()), 0});
        m_legacyCompositeActive = true;
    }
    if (m_mesh.ranges.size() != 1)
    {
        throw std::logic_error("AppendSphere can only extend the default composite mesh range.");
    }

    const SceneMesh::Range appended =
        Procedural::AppendMesh(m_mesh, Procedural::CreateSphereMesh(radius, stackCount, sliceCount), materialId);
    m_mesh.ranges[0].vertexCount += appended.vertexCount;
    m_mesh.ranges[0].indexCount += appended.indexCount;
}

SceneMeshId SceneBuilder::AddMeshRange(const SceneMesh& mesh)
{
    if (mesh.vertices.empty())
    {
        throw std::invalid_argument("Scene mesh must contain vertices.");
    }
    const SceneMesh::Range range =
        Procedural::AppendMesh(m_mesh, mesh, kGltfVertexMaterialFromInstance);
    m_mesh.ranges.push_back(range);
    return static_cast<SceneMeshId>(m_mesh.ranges.size() - 1);
}

SceneMeshId SceneBuilder::EnsureDefaultMeshRange()
{
    if (m_mesh.ranges.empty())
    {
        if (m_mesh.vertices.empty())
        {
            throw std::logic_error("AddInstance requires at least one scene mesh.");
        }
        m_mesh.ranges.push_back(
            {0,
             static_cast<uint32_t>(m_mesh.vertices.size()),
             0,
             static_cast<uint32_t>(m_mesh.indices.size())});
        m_legacyCompositeActive = true;
    }
    return 0;
}

} // namespace Engine
