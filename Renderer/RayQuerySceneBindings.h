#pragma once

#include <d3d12.h>

namespace Engine
{

struct RayQuerySceneBindings
{
    D3D12_GPU_DESCRIPTOR_HANDLE tlasSrv = {};
    D3D12_GPU_DESCRIPTOR_HANDLE cameraCbv = {};
    D3D12_GPU_DESCRIPTOR_HANDLE materialBufferSrv = {};
    D3D12_GPU_DESCRIPTOR_HANDLE textureTableSrv = {};
    D3D12_GPU_VIRTUAL_ADDRESS vertexBufferSrv = 0;
    D3D12_GPU_VIRTUAL_ADDRESS indexBufferSrv = 0;
    D3D12_GPU_VIRTUAL_ADDRESS instanceBufferSrv = 0;
    D3D12_GPU_VIRTUAL_ADDRESS meshRangeBufferSrv = 0;
    UINT usesIndexedDraw = 0;
    UINT vertexCount = 0;
    UINT indexCount = 0;
};

} // namespace Engine
