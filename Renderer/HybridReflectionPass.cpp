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
    commandList->SetComputeRootDescriptorTable(1, desc.reflectionRayColorUav);
    commandList->SetComputeRootDescriptorTable(2, desc.reflectionRayMaterialUav);
    commandList->SetComputeRootDescriptorTable(3, desc.reflectionRayEmissionUav);
    commandList->SetComputeRootDescriptorTable(4, desc.tlasSrv);
    commandList->SetComputeRootDescriptorTable(5, desc.depthSrv);
    commandList->SetComputeRootDescriptorTable(6, desc.normalSrv);
    commandList->SetComputeRootDescriptorTable(7, desc.pbrParamsSrv);
    commandList->SetComputeRootDescriptorTable(8, desc.cameraCbv);
    commandList->SetComputeRootShaderResourceView(9, desc.vertexBufferSrv);
    commandList->SetComputeRootShaderResourceView(10, desc.indexBufferSrv);
    commandList->SetComputeRootShaderResourceView(11, desc.instanceBufferSrv);
    commandList->SetComputeRootDescriptorTable(12, desc.materialBufferSrv);
    commandList->SetComputeRootDescriptorTable(13, desc.textureTableSrv);

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
    commandList->SetComputeRoot32BitConstants(14, 9, &constants, 0);

    constexpr UINT kThreadGroupSize = 8;
    const UINT dispatchX = (desc.width + kThreadGroupSize - 1) / kThreadGroupSize;
    const UINT dispatchY = (desc.height + kThreadGroupSize - 1) / kThreadGroupSize;
    commandList->Dispatch(dispatchX, dispatchY, 1);

    PIXEndEvent(commandList);
}

} // namespace Engine
