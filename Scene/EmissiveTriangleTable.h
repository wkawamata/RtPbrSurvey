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

struct EmissiveTriangleGpu
{
    DirectX::XMFLOAT3 position0;
    float area = 0.0f;
    DirectX::XMFLOAT3 position1;
    float cumulativeProbability = 0.0f;
    DirectX::XMFLOAT3 position2;
    float selectionPdf = 0.0f;
    std::array<DirectX::XMFLOAT2, 3> uvs;
    uint32_t instanceId = 0;
    uint32_t primitiveIndex = 0;
    uint32_t materialId = 0;
    uint32_t padding = 0;
};

std::vector<EmissiveTriangleGpu> SerializeEmissiveTriangleTable(const EmissiveTriangleTable& table);

} // namespace Engine
