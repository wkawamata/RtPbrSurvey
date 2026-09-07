#pragma once

#include "../Engine/Rhi/Dx12/GraphicsDevice.h"
#include "../Renderer/SimpleDescriptorHeapAllocator.h"

#include <array>
#include <cstdint>

namespace Engine
{

class ImGuiSystem
{
public:
    static constexpr UINT kMaxTextureCount = 4;

    void Initialize(HWND hwnd,
                    GraphicsDevice& device,
                    ID3D12DescriptorHeap* srvHeap,
                    UINT frameCount,
                    DXGI_FORMAT rtvFormat);
    void BeginFrame();
    void EndFrame();
    void Render(ID3D12GraphicsCommandList* commandList);
    void SetDisplaySize(UINT width, UINT height);
    uint64_t UpdateTexture(UINT textureIndex, ID3D12Resource* resource, DXGI_FORMAT format);
    void ClearTexture(UINT textureIndex);
    void ClearTextures();
    void Shutdown();

private:
    struct TextureBinding
    {
        DescriptorAllocation descriptor;
        ComPtr<ID3D12Resource> resource;
        DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    };

    SimpleDescriptorHeapAllocator m_descriptorHeapAllocator;
    std::array<TextureBinding, kMaxTextureCount> m_textureBindings;
    ID3D12Device* m_device = nullptr;
    ID3D12DescriptorHeap* m_srvHeap = nullptr;
    bool m_initialized = false;
};

} // namespace Engine
