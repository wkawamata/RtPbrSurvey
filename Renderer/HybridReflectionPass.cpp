#include "stdafx.h"

#include "HybridReflectionPass.h"

#include <pix3.h>

namespace Engine
{

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
    commandList->SetComputeRoot32BitConstants(8, 8, &desc.normalBias, 0);

    constexpr UINT kThreadGroupSize = 8;
    const UINT dispatchX = (desc.width + kThreadGroupSize - 1) / kThreadGroupSize;
    const UINT dispatchY = (desc.height + kThreadGroupSize - 1) / kThreadGroupSize;
    commandList->Dispatch(dispatchX, dispatchY, 1);

    PIXEndEvent(commandList);
}

} // namespace Engine
