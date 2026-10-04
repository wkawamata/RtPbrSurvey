#pragma once

#include "Scene.h"

#include <array>

namespace Engine
{

struct EmissiveTriangle
{
    std::array<DirectX::XMFLOAT3, 3> positions;
    std::array<DirectX::XMFLOAT2, 3> uvs;
    uint32_t instanceId = 0;
    uint32_t primitiveIndex = 0;
    uint32_t materialId = 0;
    double area = 0.0;
    double selectionPdf = 0.0;
    double cumulativeProbability = 0.0;
};

struct EmissiveTriangleTable
{
    std::vector<EmissiveTriangle> triangles;
    double totalArea = 0.0;
};

EmissiveTriangleTable BuildEmissiveTriangleTable(const Scene& scene);

} // namespace Engine
