#include "stdafx.h"

#include "SceneGeometryPass.h"

#include "RootSignatureLayout.h"

#include <pix3.h>

namespace Engine
{

void RecordSceneGeometryDraw(ID3D12GraphicsCommandList* commandList, const SceneGeometryDrawDesc& drawDesc)
{
    if (drawDesc.draws.empty())
    {
        return;
    }

    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commandList->IASetVertexBuffers(0, 1, &drawDesc.vertexBufferView);

    for (const SceneGeometryInstanceDraw& draw : drawDesc.draws)
    {
        commandList->SetGraphicsRoot32BitConstant(
            RootSignatureLayout::SceneDrawConstants, draw.instanceIndex, 0);
        if (draw.usesIndexedDraw)
        {
            commandList->IASetIndexBuffer(&drawDesc.indexBufferView);
            commandList->DrawIndexedInstanced(draw.indexCount, 1, draw.firstIndex, 0, 0);
        }
        else
        {
            commandList->DrawInstanced(draw.vertexCount, 1, draw.firstVertex, 0);
        }
    }
}

void RecordDepthPrePass(ID3D12GraphicsCommandList* commandList, const SceneGeometryDrawDesc& drawDesc)
{
    PIXBeginEvent(commandList, 0, L"DepthPrepass");

    RecordSceneGeometryDraw(commandList, drawDesc);

    PIXEndEvent(commandList);
}

void RecordForwardPass(ID3D12GraphicsCommandList* commandList, const ForwardPassDesc& passDesc)
{
    PIXBeginEvent(commandList, 0, L"ForwardPass");

    if (passDesc.renderTargets.clearColor != nullptr)
    {
        for (D3D12_CPU_DESCRIPTOR_HANDLE rtv : passDesc.renderTargets.rtvs)
        {
            commandList->ClearRenderTargetView(rtv, passDesc.renderTargets.clearColor, 0, nullptr);
        }
    }

    RecordSceneGeometryDraw(commandList, passDesc.geometryDraw);

    PIXEndEvent(commandList);
}

} // namespace Engine
