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
    D3D12_GPU_DESCRIPTOR_HANDLE normalRoughnessUav = {};
    D3D12_GPU_DESCRIPTOR_HANDLE viewZUav = {};
    D3D12_GPU_DESCRIPTOR_HANDLE motionVectorsUav = {};
    D3D12_GPU_DESCRIPTOR_HANDLE albedoUav = {};
    D3D12_GPU_DESCRIPTOR_HANDLE diffuseRadianceHitTUav = {};
    D3D12_GPU_DESCRIPTOR_HANDLE specularRadianceHitTUav = {};
    D3D12_GPU_DESCRIPTOR_HANDLE environmentMapSrv = {};
    D3D12_GPU_DESCRIPTOR_HANDLE lightCbv = {};
    RayQuerySceneBindings scene;
    float rayTMin = 0.001f;
    float rayTMax = 10000.0f;
    float normalBias = 0.01f;
    std::array<float, 3> lightDirection = {};
    std::array<float, 3> lightColor = {1.0f, 1.0f, 1.0f};
    float environmentIntensity = 0.1f;
    float diffuseIntensity = 1.0f;
    std::array<float, 4> backgroundColor = {0.0f, 0.2f, 0.4f, 1.0f};
    UINT debugOutput = 0;
    UINT maxBounces = 2;
    UINT environmentEnabled = 1;
    UINT environmentSamplingMode = 0;
    UINT skyboxEnabled = 1;
    UINT emissiveEnabled = 1;
    UINT directLightingEnabled = 1;
    UINT shadowEnabled = 1;
    UINT russianRouletteEnabled = 0;
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
