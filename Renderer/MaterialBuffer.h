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

private:
    Microsoft::WRL::ComPtr<ID3D12Resource> m_buffer;
    DescriptorAllocation m_srv;
};

} // namespace Engine
