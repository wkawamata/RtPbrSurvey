#pragma once

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

void RecordPathTracingUavClear(ID3D12GraphicsCommandList* commandList,
                               const PathTracingUavClearDesc& desc,
                               const wchar_t* eventName);

} // namespace Engine
