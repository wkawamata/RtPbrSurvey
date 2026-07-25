#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <d3d12.h>
#include <dxgiformat.h>
#include <wrl/client.h>

namespace Engine
{
struct ScreenshotReadback
{
    Microsoft::WRL::ComPtr<ID3D12Resource> resource;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout = {};
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    UINT width = 0;
    UINT height = 0;
    bool hdr10 = false;
    float paperWhiteNits = 300.0f;

    bool IsValid() const;
    void Reset();
};

void RecordScreenshotCapture(ID3D12GraphicsCommandList* commandList,
                             ID3D12Device* device,
                             ID3D12Resource* source,
                             bool hdr10,
                             float paperWhiteNits,
                             ScreenshotReadback& readback);

std::vector<std::uint8_t> ConvertScreenshotToRgba8(const std::uint8_t* sourceData,
                                                   UINT width,
                                                   UINT height,
                                                   UINT rowPitch,
                                                   DXGI_FORMAT format,
                                                   bool hdr10,
                                                   float paperWhiteNits);

bool SaveRgba8Png(
    const std::filesystem::path& path, UINT width, UINT height, const std::uint8_t* rgba8, std::string& error);

bool SaveScreenshotReadback(ScreenshotReadback& readback, const std::filesystem::path& path, std::string& error);
} // namespace Engine
