#include "stdafx.h"

#include "PathTracingPass.h"

#include <pix3.h>

namespace Engine
{
namespace
{

struct PathTracingShaderConstants
{
    UINT usesIndexedDraw;
    UINT vertexCount;
    UINT indexCount;
    UINT hitNormalSource;
    UINT debugOutput;
    UINT environmentEnabled;
    UINT emissiveEnabled;
    float rayTMin;
    float rayTMax;
    std::array<float, 3> missColor;
};

} // namespace

void RecordPathTracingUavClear(ID3D12GraphicsCommandList* commandList,
                               const PathTracingUavClearDesc& desc,
                               const wchar_t* eventName)
{
    assert(commandList != nullptr);
    assert(desc.resource != nullptr);
    assert(desc.clearColor != nullptr);

    PIXBeginEvent(commandList, 0, eventName);
    commandList->ClearUnorderedAccessViewFloat(
        desc.gpuUav, desc.cpuUav, desc.resource, desc.clearColor, 0, nullptr);
    PIXEndEvent(commandList);
}

void RecordPathTracingPass(ID3D12GraphicsCommandList* commandList, const PathTracingPassDesc& desc)
{
    assert(commandList != nullptr);
    assert(desc.rootSignature != nullptr);
    assert(desc.pipelineState != nullptr);

    PIXBeginEvent(commandList, 0, L"PathTracingPass");
    commandList->SetComputeRootSignature(desc.rootSignature);
    commandList->SetPipelineState(desc.pipelineState);
    commandList->SetComputeRootDescriptorTable(0, desc.sceneColorUav);
    commandList->SetComputeRootDescriptorTable(1, desc.scene.tlasSrv);
    commandList->SetComputeRootDescriptorTable(2, desc.scene.cameraCbv);
    commandList->SetComputeRootShaderResourceView(3, desc.scene.vertexBufferSrv);
    commandList->SetComputeRootShaderResourceView(4, desc.scene.indexBufferSrv);
    commandList->SetComputeRootShaderResourceView(5, desc.scene.instanceBufferSrv);
    commandList->SetComputeRootDescriptorTable(6, desc.scene.materialBufferSrv);
    commandList->SetComputeRootDescriptorTable(7, desc.scene.textureTableSrv);
    commandList->SetComputeRootShaderResourceView(8, desc.scene.meshRangeBufferSrv);

    const PathTracingShaderConstants constants = {
        desc.scene.usesIndexedDraw,
        desc.scene.vertexCount,
        desc.scene.indexCount,
        0,
        desc.debugOutput,
        desc.environmentEnabled,
        desc.emissiveEnabled,
        desc.rayTMin,
        desc.rayTMax,
        desc.missColor,
    };
    commandList->SetComputeRoot32BitConstants(9, 12, &constants, 0);

    constexpr UINT kThreadGroupSize = 8;
    const UINT dispatchX = (desc.width + kThreadGroupSize - 1) / kThreadGroupSize;
    const UINT dispatchY = (desc.height + kThreadGroupSize - 1) / kThreadGroupSize;
    commandList->Dispatch(dispatchX, dispatchY, 1);
    PIXEndEvent(commandList);
}

} // namespace Engine
