#pragma once

#include "Material.h"
#include "SimpleDescriptorHeapAllocator.h"

#include <d3d12.h>
#include <vector>
#include <wrl/client.h>

namespace Engine
{

class MaterialBuffer
{
public:
    void Create(ID3D12Device* device,
                SimpleDescriptorHeapAllocator& descriptorHeapAllocator,
                const std::vector<Material>& materials);
    void Update(const std::vector<Material>& materials);
    void Reset();

    DescriptorHeapHandle Srv() const
    {
        return m_srv.Handle();
    }

    DescriptorHeapHandle RawSrv() const
    {
        return m_rawSrv.Handle();
    }

    ID3D12Resource* Resource() const
    {
        return m_buffer.Get();
    }

private:
    Microsoft::WRL::ComPtr<ID3D12Resource> m_buffer;
    DescriptorAllocation m_srv;
    DescriptorAllocation m_rawSrv;
};

} // namespace Engine
