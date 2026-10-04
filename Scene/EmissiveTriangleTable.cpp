#include "stdafx.h"

#include "EmissiveTriangleTable.h"

#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>

namespace Engine
{
static_assert(sizeof(EmissiveTriangleGpu) == 88);
static_assert(offsetof(EmissiveTriangleGpu, area) == 12);
static_assert(offsetof(EmissiveTriangleGpu, cumulativeProbability) == 28);
static_assert(offsetof(EmissiveTriangleGpu, selectionPdf) == 44);
static_assert(offsetof(EmissiveTriangleGpu, uvs) == 48);
static_assert(offsetof(EmissiveTriangleGpu, instanceId) == 72);
static_assert(offsetof(EmissiveTriangleGpu, materialId) == 80);
namespace
{

bool CanEmit(const SceneMaterial& material, const SceneMesh& mesh)
{
    const float factors[] = {material.emissiveFactor.x, material.emissiveFactor.y, material.emissiveFactor.z};
    if (!std::isfinite(material.emissiveScale) || material.emissiveScale < 0.0f)
    {
        throw std::invalid_argument("Emission scale must be finite and nonnegative.");
    }
    for (const float factor : factors)
    {
        if (!std::isfinite(factor) || factor < 0.0f)
        {
            throw std::invalid_argument("Emission factors must be finite and nonnegative.");
        }
    }
    if (material.emissiveScale == 0.0f || material.emissiveTexIndex < 0)
    {
        return false;
    }
    if (static_cast<size_t>(material.emissiveTexIndex) >= mesh.textures.size())
    {
        throw std::out_of_range("Emission texture is outside the scene texture list.");
    }
    const SceneTexture& texture = mesh.textures[material.emissiveTexIndex];
    if (texture.width <= 0 || texture.height <= 0 ||
        static_cast<uint64_t>(texture.width) * texture.height > (std::numeric_limits<size_t>::max)() / 4 ||
        texture.pixels.size() != static_cast<size_t>(texture.width) * texture.height * 4)
    {
        throw std::invalid_argument("Emission texture must contain packed RGBA8 pixels.");
    }
    for (size_t pixel = 0; pixel < texture.pixels.size(); pixel += 4)
    {
        for (size_t channel = 0; channel < 3; ++channel)
        {
            if (factors[channel] > 0.0f && texture.pixels[pixel + channel] > 0)
            {
                return true;
            }
        }
    }
    return false;
}

double TriangleArea(const std::array<DirectX::XMFLOAT3, 3>& positions)
{
    const double ax = double(positions[1].x) - positions[0].x;
    const double ay = double(positions[1].y) - positions[0].y;
    const double az = double(positions[1].z) - positions[0].z;
    const double bx = double(positions[2].x) - positions[0].x;
    const double by = double(positions[2].y) - positions[0].y;
    const double bz = double(positions[2].z) - positions[0].z;
    return 0.5 * std::hypot(ay * bz - az * by, az * bx - ax * bz, ax * by - ay * bx);
}

} // namespace

EmissiveTriangleTable BuildEmissiveTriangleTable(const Scene& scene,
    std::span<const SceneMaterial> materialOverrides)
{
    EmissiveTriangleTable result;
    if (scene.instances.empty())
    {
        return result;
    }
    if (scene.mesh == nullptr || scene.instances.size() > (std::numeric_limits<uint32_t>::max)())
    {
        throw std::invalid_argument("Emitter extraction requires valid scene geometry and instance IDs.");
    }
    const SceneMesh& mesh = *scene.mesh;
    const std::span<const SceneMaterial> materials = materialOverrides.empty() ?
        std::span<const SceneMaterial>(mesh.materials) : materialOverrides;
    if (materials.size() != mesh.materials.size())
    {
        throw std::invalid_argument("Emitter material overrides must match the scene material list.");
    }
    std::vector<bool> emitting;
    for (const SceneMaterial& material : materials)
    {
        emitting.push_back(CanEmit(material, mesh));
    }
    for (size_t instanceIndex = 0; instanceIndex < scene.instances.size(); ++instanceIndex)
    {
        const InstanceData& instance = scene.instances[instanceIndex];
        if (instance.meshId >= mesh.ranges.size())
        {
            throw std::out_of_range("Emitter mesh ID is outside the mesh range list.");
        }
        const DirectX::XMMATRIX world = DirectX::XMMatrixTranspose(DirectX::XMLoadFloat4x4(&instance.world));
        DirectX::XMFLOAT4X4 matrix;
        DirectX::XMStoreFloat4x4(&matrix, world);
        for (const auto& row : matrix.m)
        {
            for (const float value : row)
            {
                if (!std::isfinite(value))
                {
                    throw std::invalid_argument("Emitter transform must be finite.");
                }
            }
        }
        if (matrix._14 != 0.0f || matrix._24 != 0.0f || matrix._34 != 0.0f || matrix._44 != 1.0f)
        {
            throw std::invalid_argument("Emitter transform must be affine.");
        }
        const float determinant = DirectX::XMVectorGetX(DirectX::XMMatrixDeterminant(world));
        if (!std::isfinite(determinant) || determinant <= 0.0f)
        {
            throw std::invalid_argument("Emitter extraction supports only positive nonsingular instance transforms.");
        }
        const SceneMesh::Range& range = mesh.ranges[instance.meshId];
        if (range.firstVertex > mesh.vertices.size() || range.vertexCount > mesh.vertices.size() - range.firstVertex ||
            range.firstIndex > mesh.indices.size() || range.indexCount > mesh.indices.size() - range.firstIndex)
        {
            throw std::out_of_range("Emitter mesh range exceeds scene geometry.");
        }
        const uint32_t count = range.indexCount != 0 ? range.indexCount : range.vertexCount;
        if (count % 3 != 0)
        {
            throw std::invalid_argument("Emitter mesh must contain complete triangles.");
        }
        for (uint32_t primitive = 0; primitive < count / 3; ++primitive)
        {
            EmissiveTriangle triangle;
            triangle.instanceId = static_cast<uint32_t>(instanceIndex);
            triangle.primitiveIndex = primitive;
            for (uint32_t corner = 0; corner < 3; ++corner)
            {
                const uint32_t vertexId = range.indexCount != 0 ?
                    mesh.indices[range.firstIndex + primitive * 3 + corner] : range.firstVertex + primitive * 3 + corner;
                if (vertexId < range.firstVertex || vertexId - range.firstVertex >= range.vertexCount)
                {
                    throw std::out_of_range("Emitter index refers outside its mesh vertex range.");
                }
                const SceneVertex& vertex = mesh.vertices[vertexId];
                DirectX::XMStoreFloat3(&triangle.positions[corner],
                    DirectX::XMVector3TransformCoord(DirectX::XMLoadFloat3(&vertex.position), world));
                triangle.uvs[corner] = vertex.uv;
                const DirectX::XMFLOAT3& position = triangle.positions[corner];
                if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z) ||
                    !std::isfinite(vertex.uv.x) || !std::isfinite(vertex.uv.y))
                {
                    throw std::invalid_argument("Emitter positions and UVs must be finite.");
                }
                if (corner == 0)
                {
                    triangle.materialId = vertex.materialId == kGltfVertexMaterialFromInstance ?
                        instance.materialId : vertex.materialId;
                }
            }
            if (triangle.materialId >= emitting.size())
            {
                throw std::out_of_range("Emitter material ID is outside the material list.");
            }
            triangle.area = TriangleArea(triangle.positions);
            if (!std::isfinite(triangle.area))
            {
                throw std::invalid_argument("Emitter area must be finite.");
            }
            if (!emitting[triangle.materialId] || triangle.area == 0.0)
            {
                continue;
            }
            result.totalArea += triangle.area;
            result.triangles.push_back(triangle);
        }
    }
    if (!std::isfinite(result.totalArea))
    {
        throw std::invalid_argument("Total emitter area must be finite.");
    }
    double cumulative = 0.0;
    for (EmissiveTriangle& triangle : result.triangles)
    {
        triangle.selectionPdf = triangle.area / result.totalArea;
        cumulative += triangle.selectionPdf;
        triangle.cumulativeProbability = cumulative;
    }
    if (!result.triangles.empty())
    {
        result.triangles.back().cumulativeProbability = 1.0;
    }
    return result;
}

std::vector<EmissiveTriangleGpu> SerializeEmissiveTriangleTable(const EmissiveTriangleTable& table)
{
    std::vector<EmissiveTriangleGpu> result;
    result.reserve(table.triangles.size());
    float previous = 0.0f;
    for (size_t index = 0; index < table.triangles.size(); ++index)
    {
        const EmissiveTriangle& source = table.triangles[index];
        EmissiveTriangleGpu triangle;
        triangle.position0 = source.positions[0];
        triangle.position1 = source.positions[1];
        triangle.position2 = source.positions[2];
        triangle.area = static_cast<float>(source.area);
        triangle.cumulativeProbability = static_cast<float>(source.cumulativeProbability);
        if (index + 1 == table.triangles.size())
        {
            if (source.cumulativeProbability != 1.0)
            {
                throw std::invalid_argument("Emitter CDF must terminate at one.");
            }
            triangle.cumulativeProbability = 1.0f;
        }
        triangle.selectionPdf = triangle.cumulativeProbability - previous;
        if (!std::isfinite(triangle.area) || triangle.area <= 0.0f ||
            !std::isfinite(triangle.cumulativeProbability) || triangle.cumulativeProbability > 1.0f ||
            triangle.selectionPdf <= 0.0f)
        {
            throw std::invalid_argument("Emitter area/CDF is not representable by the GPU float distribution.");
        }
        triangle.uvs = source.uvs;
        triangle.instanceId = source.instanceId;
        triangle.primitiveIndex = source.primitiveIndex;
        triangle.materialId = source.materialId;
        previous = triangle.cumulativeProbability;
        result.push_back(triangle);
    }
    return result;
}

} // namespace Engine
