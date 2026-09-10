#include "stdafx.h"

#include "MaterialBuffer.h"

#include "../MyDx12Utils.h"

namespace Engine
{

void MaterialBuffer::Create(ID3D12Device* device,
                            SimpleDescriptorHeapAllocator& descriptorHeapAllocator,
                            const std::vector<Material>& materials)
{
    assert(materials.size() >= kMaterialCount);
    const UINT materialBufferSize = sizeof(Material) * kMaterialCount;

    MyDx12Util::CreateUploadBuffer(device, materialBufferSize, m_buffer);

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    srvDesc.Format = DXGI_FORMAT_UNKNOWN;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Buffer.NumElements = kMaterialCount;
    srvDesc.Buffer.StructureByteStride = sizeof(Material);

    m_srv = descriptorHeapAllocator.Allocate();
    device->CreateShaderResourceView(m_buffer.Get(), &srvDesc, m_srv.Cpu());

    D3D12_SHADER_RESOURCE_VIEW_DESC rawSrvDesc = {};
    rawSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    rawSrvDesc.Format = DXGI_FORMAT_R32_TYPELESS;
    rawSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    rawSrvDesc.Buffer.NumElements = materialBufferSize / sizeof(uint32_t);
    rawSrvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
    m_rawSrv = descriptorHeapAllocator.Allocate();
    device->CreateShaderResourceView(m_buffer.Get(), &rawSrvDesc, m_rawSrv.Cpu());

    Update(materials);
}

void MaterialBuffer::Update(const std::vector<Material>& materials)
{
    assert(materials.size() >= kMaterialCount);
    assert(m_buffer != nullptr);
    const UINT materialBufferSize = sizeof(Material) * kMaterialCount;

    Material* pMaterialDataBegin = nullptr;
    m_buffer->Map(0, nullptr, reinterpret_cast<void**>(&pMaterialDataBegin));
    memcpy(pMaterialDataBegin, materials.data(), materialBufferSize);
    m_buffer->Unmap(0, nullptr);
}

void MaterialBuffer::Reset()
{
    m_buffer.Reset();
    m_srv.Reset();
    m_rawSrv.Reset();
}

} // namespace Engine
