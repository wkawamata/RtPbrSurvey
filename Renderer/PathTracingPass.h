#pragma once

#include "RayQuerySceneBindings.h"

#include <array>
#include <d3d12.h>

namespace Engine
{

struct PathTracingUavClearDesc
{
    ID3D12Resource* resource = nullptr;
    D3D12_CPU_DESCRIPTOR_HANDLE cpuUav = {};
    D3D12_GPU_DESCRIPTOR_HANDLE gpuUav = {};
    const float* clearColor = nullptr;
};

struct PathTracingPassDesc
{
    ID3D12RootSignature* rootSignature = nullptr;
    ID3D12PipelineState* pipelineState = nullptr;
    D3D12_GPU_DESCRIPTOR_HANDLE sceneColorUav = {};
    D3D12_GPU_DESCRIPTOR_HANDLE accumulationUav = {};
    RayQuerySceneBindings scene;
    std::array<float, 3> missColor = {};
    float rayTMin = 0.001f;
    float rayTMax = 10000.0f;
    UINT debugOutput = 0;
    UINT environmentEnabled = 1;
    UINT emissiveEnabled = 1;
    UINT samplesPerFrame = 1;
    UINT sampleStartIndex = 0;
    UINT randomSeed = 1;
    float previousSampleCount = 0.0f;
    bool accumulate = true;
    UINT width = 0;
    UINT height = 0;
};

void RecordPathTracingUavClear(ID3D12GraphicsCommandList* commandList,
                               const PathTracingUavClearDesc& desc,
                               const wchar_t* eventName);
void RecordPathTracingPass(ID3D12GraphicsCommandList* commandList, const PathTracingPassDesc& desc);

} // namespace Engine
