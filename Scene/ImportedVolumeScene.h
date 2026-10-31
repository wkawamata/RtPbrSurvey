#pragma once

#include "Scene/Scene.h"
#include "Scene/ConvexVolumeBounds.h"

#include <DirectXMath.h>

namespace RtPbrSurvey
{
inline std::optional<ConvexVolumeBounds> BuildImportedVolumeSceneGuard(const Engine::Scene& scene, std::string& error)
{
    error.clear();
    if (scene.mesh == nullptr)
    {
        return std::nullopt;
    }
    const Engine::SceneMesh& mesh = *scene.mesh;
    if (std::none_of(mesh.materials.begin(), mesh.materials.end(),
            [](const Engine::SceneMaterial& material) { return material.importedVolumeTransmission; }))
    {
        return std::nullopt;
    }
    std::optional<ConvexVolumeBounds> volume;
    size_t volumeInstance = 0;
    std::vector<std::array<ConvexVolumeBounds::Position, 2>> bounds;
    std::vector<size_t> boundInstances;
    for (size_t instanceIndex = 0; instanceIndex < scene.instances.size(); ++instanceIndex)
    {
        const auto& instance = scene.instances[instanceIndex];
        if (!Engine::HasRenderableTransform(instance))
        {
            continue;
        }
        if (instance.meshId >= mesh.ranges.size())
        {
            error = "Imported volume scene contains an invalid mesh range.";
            return std::nullopt;
        }
        const auto& range = mesh.ranges[instance.meshId];
        if (range.firstIndex > mesh.indices.size() || range.indexCount > mesh.indices.size() - range.firstIndex ||
            range.indexCount == 0)
        {
            error = "Imported volume scene contains invalid geometry.";
            return std::nullopt;
        }
        bool imported = false;
        bool otherVolume = false;
        uint32_t materialId = kGltfVertexMaterialFromInstance;
        std::vector<ConvexVolumeBounds::Position> positions;
        std::vector<uint32_t> indices;
        const DirectX::XMMATRIX world = DirectX::XMMatrixTranspose(DirectX::XMLoadFloat4x4(&instance.world));
        std::array<ConvexVolumeBounds::Position, 2> aabb;
        for (size_t offset = 0; offset < range.indexCount; ++offset)
        {
            const uint32_t index = mesh.indices[range.firstIndex + offset];
            if (index >= mesh.vertices.size())
            {
                error = "Imported volume scene contains an invalid vertex index.";
                return std::nullopt;
            }
            const auto& vertex = mesh.vertices[index];
            const uint32_t material = vertex.materialId == kGltfVertexMaterialFromInstance ? instance.materialId : vertex.materialId;
            if (material >= mesh.materials.size())
            {
                error = "Imported volume scene contains an invalid material index.";
                return std::nullopt;
            }
            imported = imported || mesh.materials[material].importedVolumeTransmission;
            otherVolume = otherVolume || (mesh.materials[material].volumeTransmission && !mesh.materials[material].importedVolumeTransmission);
            if (offset == 0)
            {
                materialId = material;
            }
            else if (materialId != material)
            {
                materialId = kGltfVertexMaterialFromInstance;
            }
            DirectX::XMFLOAT3 point;
            DirectX::XMStoreFloat3(&point, DirectX::XMVector3TransformCoord(DirectX::XMLoadFloat3(&vertex.position), world));
            const ConvexVolumeBounds::Position position = {point.x, point.y, point.z};
            for (size_t axis = 0; axis < 3; ++axis)
            {
                if (!std::isfinite(position[axis]))
                {
                    error = "Imported volume scene has nonfinite transformed geometry.";
                    return std::nullopt;
                }
                aabb[0][axis] = offset == 0 ? position[axis] : (std::min)(aabb[0][axis], position[axis]);
                aabb[1][axis] = offset == 0 ? position[axis] : (std::max)(aabb[1][axis], position[axis]);
            }
            indices.push_back(static_cast<uint32_t>(positions.size()));
            positions.push_back(position);
        }
        if (otherVolume)
        {
            error = "Imported volume cannot be combined with another volume.";
            return std::nullopt;
        }
        if (imported)
        {
            if (volume || materialId == kGltfVertexMaterialFromInstance)
            {
                error = "Imported volume requires one instance with a uniform boundary material.";
                return std::nullopt;
            }
            volume = ConvexVolumeBounds::Build(positions, indices, &error);
            if (!volume)
            {
                return std::nullopt;
            }
            volumeInstance = instanceIndex;
        }
        bounds.push_back(aabb);
        boundInstances.push_back(instanceIndex);
    }
    if (volume)
    {
        for (size_t index = 0; index < bounds.size(); ++index)
        {
            if (boundInstances[index] != volumeInstance && volume->MayOverlapBounds(bounds[index][0], bounds[index][1]))
            {
                error = "Imported volume requires AABB separation from every other visible object.";
                return std::nullopt;
            }
        }
    }
    return volume;
}
}
