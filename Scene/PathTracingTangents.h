#pragma once

#include "Scene.h"

#include <span>

namespace Engine
{

std::vector<DirectX::XMFLOAT4> GeneratePathTracingCornerTangents(
    std::span<const GltfVertex> vertices, std::span<const uint32_t> indices,
    size_t* unavailableCornerCount = nullptr);

struct PathTracingTangentGeometry
{
    std::vector<SceneVertex> vertices;
    std::vector<uint32_t> indices;
    uint32_t generatedCornerCount = 0;
};

PathTracingTangentGeometry BuildPathTracingTangentGeometry(const SceneMesh& mesh);

} // namespace Engine
