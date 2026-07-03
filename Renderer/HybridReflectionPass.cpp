#include "stdafx.h"

#include "HybridReflectionPass.h"

#include <pix3.h>

namespace Engine
{
namespace
{

struct HybridReflectionShaderConstants
{
    float normalBias;
    float rayTMin;
    float rayTMax;
    float maxRoughness;
    float minMetallic;
    UINT usesIndexedDraw;
    UINT vertexCount;
    UINT indexCount;
    UINT hitNormalSource;
};

} // namespace

void RecordHybridReflectionPass(ID3D12GraphicsCommandList* commandList, const HybridReflectionPassDesc& desc)
{
    PIXBeginEvent(commandList, 0, L"HybridReflectionPass");

    commandList->SetComputeRootSignature(desc.rootSignature);
    commandList->SetPipelineState(desc.pipelineState);
    commandList->SetComputeRootDescriptorTable(0, desc.reflectionRayHitUav);
    commandList->SetComputeRootDescriptorTable(1, desc.tlasSrv);
    commandList->SetComputeRootDescriptorTable(2, desc.depthSrv);
    commandList->SetComputeRootDescriptorTable(3, desc.normalSrv);
    commandList->SetComputeRootDescriptorTable(4, desc.pbrParamsSrv);
    commandList->SetComputeRootDescriptorTable(5, desc.cameraCbv);
    commandList->SetComputeRootShaderResourceView(6, desc.vertexBufferSrv);
    commandList->SetComputeRootShaderResourceView(7, desc.indexBufferSrv);
    commandList->SetComputeRootShaderResourceView(8, desc.instanceBufferSrv);
    commandList->SetComputeRootDescriptorTable(9, desc.materialBufferSrv);
    commandList->SetComputeRootDescriptorTable(10, desc.textureTableSrv);

    const HybridReflectionShaderConstants constants = {
        desc.normalBias,
        desc.rayTMin,
        desc.rayTMax,
        desc.maxRoughness,
        desc.minMetallic,
        desc.usesIndexedDraw,
        desc.vertexCount,
        desc.indexCount,
        desc.hitNormalSource};
    commandList->SetComputeRoot32BitConstants(11, 9, &constants, 0);

    constexpr UINT kThreadGroupSize = 8;
    const UINT dispatchX = (desc.width + kThreadGroupSize - 1) / kThreadGroupSize;
    const UINT dispatchY = (desc.height + kThreadGroupSize - 1) / kThreadGroupSize;
    commandList->Dispatch(dispatchX, dispatchY, 1);

    PIXEndEvent(commandList);
}

} // namespace Engine
