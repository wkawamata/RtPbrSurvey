#include "PathTracingTangents.h"

#include "../third_party/mikktspace/mikktspace.h"

#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

namespace Engine
{
namespace
{

struct TangentContext
{
    std::span<const GltfVertex> vertices;
    std::span<const uint32_t> indices;
    std::vector<size_t> faces;
    std::vector<DirectX::XMFLOAT4> tangents;

    const GltfVertex& Vertex(int face, int corner) const
    {
        return vertices[indices[faces[face] + corner]];
    }
};

bool Finite(const GltfVertex& vertex)
{
    return std::isfinite(vertex.position.x) && std::isfinite(vertex.position.y) &&
        std::isfinite(vertex.position.z) && std::isfinite(vertex.normal.x) &&
        std::isfinite(vertex.normal.y) && std::isfinite(vertex.normal.z) &&
        std::isfinite(vertex.uv.x) && std::isfinite(vertex.uv.y);
}

bool ValidTangent(const DirectX::XMFLOAT4& value)
{
    const float lengthSquared = value.x * value.x + value.y * value.y + value.z * value.z;
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z) &&
        std::isfinite(value.w) && std::abs(lengthSquared - 1.0f) < 0.001f &&
        std::abs(std::abs(value.w) - 1.0f) < 0.0001f;
}

TangentContext& Data(const SMikkTSpaceContext* context)
{
    return *static_cast<TangentContext*>(context->m_pUserData);
}

} // namespace

std::vector<DirectX::XMFLOAT4> GeneratePathTracingCornerTangents(
    std::span<const GltfVertex> vertices, std::span<const uint32_t> indices, size_t* unavailableCornerCount)
{
    if (indices.size() % 3 != 0 || indices.size() / 3 > static_cast<size_t>((std::numeric_limits<int>::max)()))
    {
        throw std::invalid_argument("PT tangent generation requires a bounded triangle list.");
    }
    TangentContext data = {vertices, indices};
    data.tangents.resize(indices.size());
    for (size_t first = 0; first < indices.size(); first += 3)
    {
        for (size_t corner = 0; corner < 3; ++corner)
        {
            const uint32_t index = indices[first + corner];
            if (index >= vertices.size() || !Finite(vertices[index]))
            {
                throw std::invalid_argument("PT tangent generation received invalid geometry.");
            }
            const auto& normal = vertices[index].normal;
            if (std::hypot(normal.x, normal.y, normal.z) < 0.000001f)
            {
                throw std::invalid_argument("PT tangent generation requires nonzero normals.");
            }
        }
        const auto& a = vertices[indices[first]];
        const auto& b = vertices[indices[first + 1]];
        const auto& c = vertices[indices[first + 2]];
        const double ux = static_cast<double>(b.uv.x) - a.uv.x;
        const double uy = static_cast<double>(b.uv.y) - a.uv.y;
        const double vx = static_cast<double>(c.uv.x) - a.uv.x;
        const double vy = static_cast<double>(c.uv.y) - a.uv.y;
        const double determinant = ux * vy - uy * vx;
        const double px = static_cast<double>(b.position.x) - a.position.x;
        const double py = static_cast<double>(b.position.y) - a.position.y;
        const double pz = static_cast<double>(b.position.z) - a.position.z;
        const double qx = static_cast<double>(c.position.x) - a.position.x;
        const double qy = static_cast<double>(c.position.y) - a.position.y;
        const double qz = static_cast<double>(c.position.z) - a.position.z;
        const double cx = py * qz - pz * qy;
        const double cy = pz * qx - px * qz;
        const double cz = px * qy - py * qx;
        if (determinant != 0.0 && cx * cx + cy * cy + cz * cz > 0.0)
        {
            data.faces.push_back(first);
        }
    }
    size_t unavailable = indices.size() - data.faces.size() * 3;
    if (unavailableCornerCount != nullptr)
    {
        *unavailableCornerCount = unavailable;
    }
    if (data.faces.empty())
    {
        return data.tangents;
    }

    SMikkTSpaceInterface callbacks = {};
    callbacks.m_getNumFaces = [](const SMikkTSpaceContext* c) { return static_cast<int>(Data(c).faces.size()); };
    callbacks.m_getNumVerticesOfFace = [](const SMikkTSpaceContext*, int) { return 3; };
    callbacks.m_getPosition = [](const SMikkTSpaceContext* c, float out[], int face, int corner)
    {
        const auto& p = Data(c).Vertex(face, corner).position;
        out[0] = p.x;
        out[1] = p.y;
        out[2] = p.z;
    };
    callbacks.m_getNormal = [](const SMikkTSpaceContext* c, float out[], int face, int corner)
    {
        const auto& n = Data(c).Vertex(face, corner).normal;
        const double length = std::hypot(static_cast<double>(n.x), n.y, n.z);
        out[0] = static_cast<float>(n.x / length);
        out[1] = static_cast<float>(n.y / length);
        out[2] = static_cast<float>(n.z / length);
    };
    callbacks.m_getTexCoord = [](const SMikkTSpaceContext* c, float out[], int face, int corner)
    {
        const auto& uv = Data(c).Vertex(face, corner).uv;
        out[0] = uv.x;
        out[1] = uv.y;
    };
    callbacks.m_setTSpaceBasic = [](const SMikkTSpaceContext* c, const float tangent[], float sign, int face, int corner)
    {
        auto& d = Data(c);
        d.tangents[d.faces[face] + corner] = {tangent[0], tangent[1], tangent[2], sign};
    };
    SMikkTSpaceContext context = {&callbacks, &data};
    if (!genTangSpaceDefault(&context))
    {
        throw std::runtime_error("MikkTSpace could not generate PT tangents.");
    }
    for (size_t first : data.faces)
    {
        bool degenerate = false;
        for (size_t corner = 0; corner < 3; ++corner)
        {
            auto& value = data.tangents[first + corner];
            if (!std::isfinite(value.x) || !std::isfinite(value.y) || !std::isfinite(value.z) ||
                !std::isfinite(value.w) || std::abs(value.w) != 1.0f)
            {
                throw std::runtime_error("MikkTSpace generated an invalid PT tangent at corner " +
                    std::to_string(first + corner) + ": " + std::to_string(value.x) + ", " +
                    std::to_string(value.y) + ", " + std::to_string(value.z) + ", " + std::to_string(value.w));
            }
            const double length = std::hypot(static_cast<double>(value.x), value.y, value.z);
            if (length <= 0.000001)
            {
                degenerate = true;
            }
            else
            {
                value.x = static_cast<float>(value.x / length);
                value.y = static_cast<float>(value.y / length);
                value.z = static_cast<float>(value.z / length);
            }
        }
        if (degenerate)
        {
            unavailable += 3;
            for (size_t corner = 0; corner < 3; ++corner)
            {
                data.tangents[first + corner] = {};
            }
        }
    }
    if (unavailableCornerCount != nullptr)
    {
        *unavailableCornerCount = unavailable;
    }
    return data.tangents;
}

PathTracingTangentGeometry BuildPathTracingTangentGeometry(const SceneMesh& mesh)
{
    PathTracingTangentGeometry result;
    if (mesh.pathTracingCornerTangents.empty())
    {
        return result;
    }
    if (mesh.pathTracingCornerTangents.size() != mesh.indices.size())
    {
        throw std::invalid_argument("PT corner tangents must align with scene indices.");
    }
    for (size_t corner = 0; corner < mesh.indices.size(); ++corner)
    {
        const auto& tangent = mesh.pathTracingCornerTangents[corner];
        if (tangent.x == 0 && tangent.y == 0 && tangent.z == 0 && tangent.w == 0)
        {
            continue;
        }
        if (!ValidTangent(tangent))
        {
            throw std::invalid_argument("PT corner tangent is not a finite unit frame.");
        }
        const uint32_t index = mesh.indices[corner];
        if (index >= mesh.vertices.size())
        {
            throw std::invalid_argument("PT corner tangent references an invalid vertex.");
        }
        const auto& original = mesh.vertices[index];
        if (original.tangent.x != 0.0f || original.tangent.y != 0.0f || original.tangent.z != 0.0f)
        {
            continue;
        }
        if (result.vertices.empty())
        {
            result.vertices = mesh.vertices;
            result.indices = mesh.indices;
        }
        if (result.vertices.size() >= (std::numeric_limits<uint32_t>::max)())
        {
            throw std::overflow_error("PT tangent geometry exceeds vertex index capacity.");
        }
        SceneVertex duplicate = original;
        duplicate.tangent = tangent;
        result.indices[corner] = static_cast<uint32_t>(result.vertices.size());
        result.vertices.push_back(duplicate);
        ++result.generatedCornerCount;
    }
    return result;
}

} // namespace Engine
