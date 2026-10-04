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
    UINT samplesPerFrame;
    UINT sampleStartIndex;
    UINT randomSeed;
    float previousSampleCount;
    float rayTMin;
    float rayTMax;
    UINT maxBounces;
    UINT directLightingEnabled;
    UINT shadowEnabled;
    float normalBias;
    std::array<float, 3> lightDirection;
    float environmentIntensity;
    std::array<float, 3> lightColor;
    float diffuseIntensity;
    UINT russianRouletteEnabled;
    UINT skyboxEnabled;
    UINT environmentSamplingMode;
    std::array<float, 4> backgroundColor;
};

static_assert(sizeof(PathTracingShaderConstants) == 32 * sizeof(UINT));

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
    commandList->SetComputeRootDescriptorTable(1, desc.accumulationUav);
    commandList->SetComputeRootDescriptorTable(2, desc.normalRoughnessUav);
    commandList->SetComputeRootDescriptorTable(3, desc.viewZUav);
    commandList->SetComputeRootDescriptorTable(4, desc.motionVectorsUav);
    commandList->SetComputeRootDescriptorTable(5, desc.albedoUav);
    commandList->SetComputeRootDescriptorTable(6, desc.diffuseRadianceHitTUav);
    commandList->SetComputeRootDescriptorTable(7, desc.specularRadianceHitTUav);
    commandList->SetComputeRootDescriptorTable(8, desc.scene.tlasSrv);
    commandList->SetComputeRootDescriptorTable(9, desc.scene.cameraCbv);
    commandList->SetComputeRootShaderResourceView(10, desc.scene.vertexBufferSrv);
    commandList->SetComputeRootShaderResourceView(11, desc.scene.indexBufferSrv);
    commandList->SetComputeRootShaderResourceView(12, desc.scene.instanceBufferSrv);
    commandList->SetComputeRootDescriptorTable(13, desc.scene.materialBufferSrv);
    commandList->SetComputeRootDescriptorTable(14, desc.scene.textureTableSrv);
    commandList->SetComputeRootShaderResourceView(15, desc.scene.meshRangeBufferSrv);
    commandList->SetComputeRootDescriptorTable(16, desc.environmentMapSrv);
    commandList->SetComputeRootDescriptorTable(18, desc.lightCbv);

    const PathTracingShaderConstants constants = {
        desc.scene.usesIndexedDraw,
        desc.scene.vertexCount,
        desc.scene.indexCount,
        0,
        desc.debugOutput,
        desc.environmentEnabled,
        desc.emissiveEnabled,
        desc.samplesPerFrame,
        desc.sampleStartIndex,
        desc.randomSeed,
        desc.previousSampleCount,
        desc.rayTMin,
        desc.rayTMax,
        desc.maxBounces,
        desc.directLightingEnabled,
        desc.shadowEnabled,
        desc.normalBias,
        desc.lightDirection,
        desc.environmentIntensity,
        desc.lightColor,
        desc.diffuseIntensity,
        desc.russianRouletteEnabled,
        desc.skyboxEnabled,
        desc.environmentSamplingMode,
        desc.backgroundColor,
    };
    commandList->SetComputeRoot32BitConstants(17, 32, &constants, 0);

    constexpr UINT kThreadGroupSize = 8;
    const UINT dispatchX = (desc.width + kThreadGroupSize - 1) / kThreadGroupSize;
    const UINT dispatchY = (desc.height + kThreadGroupSize - 1) / kThreadGroupSize;
    commandList->Dispatch(dispatchX, dispatchY, 1);
    PIXEndEvent(commandList);
}

} // namespace Engine
