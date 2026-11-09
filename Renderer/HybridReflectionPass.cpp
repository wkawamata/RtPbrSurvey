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
    UINT stochasticSamplingEnabled;
    UINT samplingFrameIndex;
    UINT usesIndexedDraw;
    UINT vertexCount;
    UINT indexCount;
    UINT hitNormalSource;
    UINT roughEnvironmentOcclusionEnabled;
    UINT roughEnvironmentRadianceWeighted;
    UINT roughEnvironmentIntegrateRgb;
    UINT forceMirrorEnvironmentOcclusion;
};
static_assert(sizeof(HybridReflectionShaderConstants) == 15 * sizeof(UINT));

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
    commandList->SetComputeRootDescriptorTable(4, desc.scene.tlasSrv);
    commandList->SetComputeRootDescriptorTable(5, desc.depthSrv);
    commandList->SetComputeRootDescriptorTable(6, desc.normalSrv);
    commandList->SetComputeRootDescriptorTable(7, desc.pbrParamsSrv);
    commandList->SetComputeRootDescriptorTable(8, desc.scene.cameraCbv);
    commandList->SetComputeRootShaderResourceView(9, desc.scene.vertexBufferSrv);
    commandList->SetComputeRootShaderResourceView(10, desc.scene.indexBufferSrv);
    commandList->SetComputeRootShaderResourceView(11, desc.scene.instanceBufferSrv);
    commandList->SetComputeRootDescriptorTable(12, desc.scene.materialBufferSrv);
    commandList->SetComputeRootDescriptorTable(13, desc.scene.textureTableSrv);
    commandList->SetComputeRootShaderResourceView(14, desc.scene.meshRangeBufferSrv);

    const HybridReflectionShaderConstants constants = {
        desc.normalBias,
        desc.rayTMin,
        desc.rayTMax,
        desc.maxRoughness,
        desc.minMetallic,
        desc.stochasticSamplingEnabled,
        desc.samplingFrameIndex,
        desc.scene.usesIndexedDraw,
        desc.scene.vertexCount,
        desc.scene.indexCount,
        desc.hitNormalSource,
        desc.roughEnvironmentOcclusionEnabled,
        desc.roughEnvironmentRadianceWeighted,
        desc.roughEnvironmentIntegrateRgb,
        desc.forceMirrorEnvironmentOcclusion};
    commandList->SetComputeRoot32BitConstants(15, sizeof(constants) / sizeof(UINT), &constants, 0);
    commandList->SetComputeRootDescriptorTable(16, desc.environmentSrv);
    commandList->SetComputeRootDescriptorTable(17, desc.albedoSrv);
    commandList->SetComputeRootDescriptorTable(18, desc.roughEnvironmentRgbUav);

    constexpr UINT kThreadGroupSize = 8;
    const UINT dispatchX = (desc.width + kThreadGroupSize - 1) / kThreadGroupSize;
    const UINT dispatchY = (desc.height + kThreadGroupSize - 1) / kThreadGroupSize;
    commandList->Dispatch(dispatchX, dispatchY, 1);

    PIXEndEvent(commandList);
}

} // namespace Engine
