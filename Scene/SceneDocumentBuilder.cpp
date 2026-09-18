#include "stdafx.h"

#include "Scene/SceneDocumentBuilder.h"
#include "Scene/SceneDocumentJson.h"

#include <DirectXMath.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <unordered_map>

namespace RtPbrSurvey
{
namespace
{
uint8_t LinearToSrgb8(float value)
{
    const float clamped = std::clamp(value, 0.0f, 1.0f);
    const float srgb = clamped <= 0.0031308f ? clamped * 12.92f : 1.055f * std::pow(clamped, 1.0f / 2.4f) - 0.055f;
    return static_cast<uint8_t>(std::lround(std::clamp(srgb, 0.0f, 1.0f) * 255.0f));
}

std::string PrimitiveMeshKey(const ScenePrimitive& primitive)
{
    return std::to_string(static_cast<int>(primitive.kind)) + ":" + std::to_string(primitive.size) + ":" +
           std::to_string(primitive.radius) + ":" + std::to_string(primitive.height) + ":" +
           std::to_string(primitive.width) + ":" + std::to_string(primitive.depth) + ":" +
           std::to_string(primitive.stacks) + ":" + std::to_string(primitive.slices) + ":" +
           std::to_string(primitive.radialSegments);
}

Engine::SceneMeshId AddPrimitiveMesh(Engine::SceneBuilder& builder, const ScenePrimitive& primitive)
{
    switch (primitive.kind)
    {
    case ScenePrimitiveKind::Cube:
        return builder.AddCube(primitive.size);
    case ScenePrimitiveKind::Sphere:
        return builder.AddSphere(primitive.radius, static_cast<int>(primitive.stacks), static_cast<int>(primitive.slices));
    case ScenePrimitiveKind::Plane:
        return builder.AddPlane(primitive.width, primitive.depth);
    case ScenePrimitiveKind::Cylinder:
        return builder.AddCylinder(primitive.radius,
                                   primitive.height,
                                   primitive.radialSegments,
                                   Engine::CylinderCapMode::Both);
    }
    throw std::invalid_argument("Unknown scene primitive kind.");
}

bool ResolveGltfAssetPath(const SceneAsset& asset,
                          const std::filesystem::path& sceneDirectory,
                          std::filesystem::path& resolvedPath,
                          std::string* error)
{
    const std::filesystem::path relativePath(asset.path);
    if (relativePath.empty() || relativePath.is_absolute())
    {
        if (error != nullptr)
        {
            *error = "glTF asset '" + asset.id + "' must use a non-empty relative path.";
        }
        return false;
    }

    resolvedPath = (sceneDirectory / relativePath).lexically_normal();
    std::error_code filesystemError;
    if (!std::filesystem::is_regular_file(resolvedPath, filesystemError))
    {
        if (error != nullptr)
        {
            *error = "glTF asset '" + asset.id + "' was not found: " + resolvedPath.generic_string();
        }
        return false;
    }
    return true;
}

} // namespace

bool SceneDocumentBuilder::Build(const SceneDocument& document, std::string* error)
{
    return Build(document, {}, error);
}

bool SceneDocumentBuilder::Build(const SceneDocument& document,
                                 const std::filesystem::path& sceneDirectory,
                                 std::string* error)
{
    if (!ValidateSceneDocument(document, error))
    {
        return false;
    }

    SceneGraphEvaluation graph;
    if (!EvaluateSceneGraph(document, graph, error))
    {
        return false;
    }
    std::unordered_map<std::string, std::filesystem::path> gltfPaths;
    for (const SceneAsset& asset : document.assets)
    {
        std::filesystem::path resolvedPath;
        if (!ResolveGltfAssetPath(asset, sceneDirectory, resolvedPath, error))
        {
            return false;
        }
        gltfPaths.emplace(asset.id, std::move(resolvedPath));
    }

    // Validate every referenced glTF before mutating the builder. This keeps the
    // previously published scene usable when an asset is missing or malformed.
    for (const SceneNode& node : document.nodes)
    {
        if (node.type != SceneNodeType::Gltf || !node.visible)
        {
            continue;
        }
        const auto asset = gltfPaths.find(node.assetId);
        if (asset == gltfPaths.end())
        {
            if (error != nullptr)
            {
                *error = "glTF node '" + node.id + "' references an unavailable asset: " + node.assetId;
            }
            return false;
        }
        GltfMeshData mesh = {};
        if (!::LoadGltfMesh(asset->second.string(), mesh))
        {
            if (error != nullptr)
            {
                *error = "glTF asset '" + node.assetId + "' could not be loaded: " + asset->second.generic_string();
            }
            return false;
        }
    }

    try
    {
        m_builder.Clear();
        SceneDocumentBuildResult result;
        result.graph = std::move(graph);
        for (const SceneMaterial& material : document.materials)
        {
            const std::array<uint8_t, 4> pixels = {
                LinearToSrgb8(material.baseColor.x),
                LinearToSrgb8(material.baseColor.y),
                LinearToSrgb8(material.baseColor.z),
                255,
            };
            const uint32_t textureId = m_builder.AddTextureRGBA8(1, 1, pixels);
            Engine::SceneMaterial rendererMaterial = {};
            rendererMaterial.albedoTexIndex = static_cast<int>(textureId);
            rendererMaterial.metallicRoughnessTexIndex = textureId;
            rendererMaterial.occlusionTexIndex = textureId;
            rendererMaterial.emissiveTexIndex = textureId;
            rendererMaterial.normalTexIndex = -1;
            rendererMaterial.metallicFactor = material.metallic;
            rendererMaterial.roughnessFactor = material.roughness;
            result.materialIds.emplace(material.id, m_builder.AddMaterial(rendererMaterial));
        }

        std::unordered_map<std::string, Engine::SceneMeshId> primitiveMeshIds;
        std::unordered_map<std::string, Engine::SceneMeshId> gltfMeshIds;
        for (size_t index = 0; index < document.nodes.size(); ++index)
        {
            const SceneNode& node = document.nodes[index];
            if (node.type == SceneNodeType::Empty || !node.visible)
            {
                continue;
            }
            if (node.type == SceneNodeType::Gltf)
            {
                const auto [mesh, inserted] = gltfMeshIds.emplace(node.assetId, Engine::SceneMeshId{});
                if (inserted)
                {
                    const auto asset = gltfPaths.find(node.assetId);
                    const std::optional<Engine::SceneMeshId> addedMesh = m_builder.AddGltfMesh(asset->second.string());
                    if (!addedMesh)
                    {
                        if (error != nullptr)
                        {
                            *error = "glTF asset '" + node.assetId + "' could not be converted: " +
                                     asset->second.generic_string();
                        }
                        return false;
                    }
                    mesh->second = *addedMesh;
                }

                const Engine::SceneMesh& sceneMesh = m_builder.GetMesh();
                const uint32_t instanceMaterial = sceneMesh.materials.empty() ?
                                                      m_builder.AddMaterial({}) :
                                                      0;
                const DirectX::XMMATRIX world = DirectX::XMLoadFloat4x4(&result.graph.worldTransforms[index]);
                m_builder.AddInstance(mesh->second, world, instanceMaterial);
                result.nodeInstanceIndices.emplace(node.id, m_builder.GetScene().instances.size() - 1);
                continue;
            }
            const std::string key = PrimitiveMeshKey(node.primitive);
            const auto [mesh, inserted] = primitiveMeshIds.emplace(key, Engine::SceneMeshId{});
            if (inserted)
            {
                mesh->second = AddPrimitiveMesh(m_builder, node.primitive);
            }
            const auto material = result.materialIds.find(node.materialId);
            if (material == result.materialIds.end())
            {
                if (error != nullptr)
                {
                    *error = "Primitive material is unavailable: " + node.materialId;
                }
                return false;
            }
            const DirectX::XMMATRIX world = DirectX::XMLoadFloat4x4(&result.graph.worldTransforms[index]);
            m_builder.AddInstance(mesh->second, world, material->second);
            result.nodeInstanceIndices.emplace(node.id, m_builder.GetScene().instances.size() - 1);
        }

        Engine::CameraState camera = {};
        camera.pos = {document.camera.position.x, document.camera.position.y, document.camera.position.z};
        camera.gazePoint = {document.camera.target.x, document.camera.target.y, document.camera.target.z};
        camera.up = {document.camera.up.x, document.camera.up.y, document.camera.up.z};
        camera.projection = document.camera.projection == SceneCameraProjection::Perspective
                                ? Engine::CameraProjection::Perspective
                                : Engine::CameraProjection::Orthographic;
        camera.fov = document.camera.verticalFovDegrees;
        camera.orthographicHeight = document.camera.orthographicHeight;
        camera.nearZ = document.camera.nearZ;
        camera.farZ = document.camera.farZ;
        m_builder.SetCamera(camera);
        m_result = std::move(result);
        if (error != nullptr)
        {
            error->clear();
        }
        return true;
    }
    catch (const std::exception& exception)
    {
        if (error != nullptr)
        {
            *error = exception.what();
        }
        return false;
    }
}

} // namespace RtPbrSurvey
