#pragma once

#include "SimpleDescriptorHeapAllocator.h"

#include <d3d12.h>
#include <wrl/client.h>

namespace Engine
{

struct InstanceData;

struct AccelerationStructureResources
{
    Microsoft::WRL::ComPtr<ID3D12Resource> blas;
    Microsoft::WRL::ComPtr<ID3D12Resource> blasScratch;
    Microsoft::WRL::ComPtr<ID3D12Resource> tlas;
    Microsoft::WRL::ComPtr<ID3D12Resource> tlasScratch;
    DescriptorAllocation tlasSrv;

    void Build(
        ID3D12Device* device,
        ID3D12GraphicsCommandList* commandList,
        ID3D12Resource* vertexBuffer,
        ID3D12Resource* indexBuffer,
        UINT vertexCountPerInstance,
        UINT indexCountPerInstance,
        bool usesIndexedDraw,
        const InstanceData* instances,
        UINT instanceCount,
        ID3D12Resource* tlasInstanceBuffer,
        SimpleDescriptorHeapAllocator& descriptorHeapAllocator);

    void RebuildTlas(
        ID3D12Device* device,
        ID3D12GraphicsCommandList* commandList,
        const InstanceData* instances,
        UINT instanceCount,
        ID3D12Resource* tlasInstanceBuffer);

    void Release();
};

} // namespace Engine
