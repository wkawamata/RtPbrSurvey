#pragma once

#include "../Engine/Rhi/Dx12/GraphicsDevice.h"
#include "../Renderer/SimpleDescriptorHeapAllocator.h"

#include <cstdint>

namespace Engine
{

class ImGuiSystem
{
public:
    void Initialize(HWND hwnd,
                    GraphicsDevice& device,
                    ID3D12DescriptorHeap* srvHeap,
                    UINT frameCount,
                    DXGI_FORMAT rtvFormat);
    void BeginFrame();
    void EndFrame();
    void Render(ID3D12GraphicsCommandList* commandList);
    void SetDisplaySize(UINT width, UINT height);
    uint64_t UpdateTexture(ID3D12Resource* resource, DXGI_FORMAT format);
    void ClearTexture();
    void Shutdown();

private:
    SimpleDescriptorHeapAllocator m_descriptorHeapAllocator;
    DescriptorAllocation m_textureDescriptor;
    ID3D12Resource* m_textureResource = nullptr;
    DXGI_FORMAT m_textureFormat = DXGI_FORMAT_UNKNOWN;
    ID3D12Device* m_device = nullptr;
    ID3D12DescriptorHeap* m_srvHeap = nullptr;
    bool m_initialized = false;
};

} // namespace Engine
