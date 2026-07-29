#pragma once

#include "ResolvedRenderTargets.h"

#include <d3d12.h>
#include <span>

namespace Engine
{

struct SceneGeometryInstanceDraw
{
    bool usesIndexedDraw = false;
    UINT vertexCount = 0;
    UINT indexCount = 0;
    UINT firstVertex = 0;
    UINT firstIndex = 0;
    UINT instanceIndex = 0;
};

struct SceneGeometryDrawDesc
{
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView = {};
    D3D12_INDEX_BUFFER_VIEW indexBufferView = {};
    std::span<const SceneGeometryInstanceDraw> draws;
};

struct ForwardPassDesc
{
    ResolvedRenderTargets renderTargets;
    SceneGeometryDrawDesc geometryDraw = {};
};

void RecordSceneGeometryDraw(ID3D12GraphicsCommandList* commandList, const SceneGeometryDrawDesc& drawDesc);
void RecordDepthPrePass(ID3D12GraphicsCommandList* commandList, const SceneGeometryDrawDesc& drawDesc);
void RecordForwardPass(ID3D12GraphicsCommandList* commandList, const ForwardPassDesc& passDesc);

} // namespace Engine
