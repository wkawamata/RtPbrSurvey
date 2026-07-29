#pragma once

#include "SimpleDescriptorHeapAllocator.h"

#include <d3d12.h>
#include <span>
#include <vector>
#include <wrl/client.h>

namespace Engine
{

struct InstanceData;

struct AccelerationStructureGeometry
{
    UINT firstVertex = 0;
    UINT vertexCount = 0;
    UINT firstIndex = 0;
    UINT indexCount = 0;
};

struct AccelerationStructureResources
{
    struct BlasResources
    {
        Microsoft::WRL::ComPtr<ID3D12Resource> result;
        Microsoft::WRL::ComPtr<ID3D12Resource> scratch;
    };

    std::vector<BlasResources> blases;
    Microsoft::WRL::ComPtr<ID3D12Resource> tlas;
    Microsoft::WRL::ComPtr<ID3D12Resource> tlasScratch;
    DescriptorAllocation tlasSrv;

    void Build(
        ID3D12Device* device,
        ID3D12GraphicsCommandList* commandList,
        ID3D12Resource* vertexBuffer,
        ID3D12Resource* indexBuffer,
        UINT totalVertexCount,
        std::span<const AccelerationStructureGeometry> geometries,
        const InstanceData* instances,
        UINT instanceCount,
        ID3D12Resource* tlasInstanceBuffer,
        SimpleDescriptorHeapAllocator& descriptorHeapAllocator);

    void RebuildTlas(
        ID3D12Device* device,
        ID3D12GraphicsCommandList* commandList,
        std::span<const AccelerationStructureGeometry> geometries,
        const InstanceData* instances,
        UINT instanceCount,
        ID3D12Resource* tlasInstanceBuffer);

    void Release();
};

} // namespace Engine
