#include "stdafx.h"

#include "ScreenshotCapture.h"

#include "../Shared/Error.h"
#include "HdrOutput.h"

#include <algorithm>
#include <cmath>
#include <combaseapi.h>
#include <wincodec.h>

namespace Engine
{
namespace
{
class ScopedComInitialization
{
public:
    ScopedComInitialization()
    {
        m_result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    }

    ~ScopedComInitialization()
    {
        if (SUCCEEDED(m_result))
        {
            CoUninitialize();
        }
    }

    bool IsAvailable() const
    {
        return SUCCEEDED(m_result) || m_result == RPC_E_CHANGED_MODE;
    }

private:
    HRESULT m_result = E_FAIL;
};

std::uint8_t ToByte(float value)
{
    return static_cast<std::uint8_t>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
}

float LinearToSrgb(float value)
{
    value = std::clamp(value, 0.0f, 1.0f);
    return value <= 0.0031308f ? 12.92f * value : 1.055f * std::pow(value, 1.0f / 2.4f) - 0.055f;
}

void ConvertHdr10Pixel(float& r, float& g, float& b, float paperWhiteNits)
{
    const float rec2020R = St2084PqToNits(r);
    const float rec2020G = St2084PqToNits(g);
    const float rec2020B = St2084PqToNits(b);

    const float rec709R = 1.660491f * rec2020R - 0.587641f * rec2020G - 0.072850f * rec2020B;
    const float rec709G = -0.124550f * rec2020R + 1.132900f * rec2020G - 0.008349f * rec2020B;
    const float rec709B = -0.018151f * rec2020R - 0.100579f * rec2020G + 1.118730f * rec2020B;
    const float white = (std::max)(paperWhiteNits, 1.0f);

    r = LinearToSrgb(rec709R / white);
    g = LinearToSrgb(rec709G / white);
    b = LinearToSrgb(rec709B / white);
}

std::string HResultMessage(HRESULT result)
{
    char message[48] = {};
    sprintf_s(message, "HRESULT 0x%08X", static_cast<unsigned int>(result));
    return message;
}
} // namespace

bool ScreenshotReadback::IsValid() const
{
    return resource != nullptr;
}

void ScreenshotReadback::Reset()
{
    resource.Reset();
    layout = {};
    format = DXGI_FORMAT_UNKNOWN;
    width = 0;
    height = 0;
    hdr10 = false;
    paperWhiteNits = 300.0f;
}

void RecordScreenshotCapture(ID3D12GraphicsCommandList* commandList,
                             ID3D12Device* device,
                             ID3D12Resource* source,
                             bool hdr10,
                             float paperWhiteNits,
                             ScreenshotReadback& readback)
{
    const D3D12_RESOURCE_DESC desc = source->GetDesc();
    UINT numRows = 0;
    UINT64 rowSizeInBytes = 0;
    UINT64 totalBytes = 0;
    device->GetCopyableFootprints(&desc, 0, 1, 0, &readback.layout, &numRows, &rowSizeInBytes, &totalBytes);

    const CD3DX12_HEAP_PROPERTIES heapProperties(D3D12_HEAP_TYPE_READBACK);
    const CD3DX12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(totalBytes);
    ThrowIfFailed(device->CreateCommittedResource(&heapProperties,
                                                  D3D12_HEAP_FLAG_NONE,
                                                  &bufferDesc,
                                                  D3D12_RESOURCE_STATE_COPY_DEST,
                                                  nullptr,
                                                  IID_PPV_ARGS(&readback.resource)));

    const CD3DX12_TEXTURE_COPY_LOCATION destination(readback.resource.Get(), readback.layout);
    const CD3DX12_TEXTURE_COPY_LOCATION sourceLocation(source, 0);
    commandList->CopyTextureRegion(&destination, 0, 0, 0, &sourceLocation, nullptr);

    readback.format = desc.Format;
    readback.width = static_cast<UINT>(desc.Width);
    readback.height = desc.Height;
    readback.hdr10 = hdr10;
    readback.paperWhiteNits = paperWhiteNits;
}

std::vector<std::uint8_t> ConvertScreenshotToRgba8(const std::uint8_t* sourceData,
                                                   UINT width,
                                                   UINT height,
                                                   UINT rowPitch,
                                                   DXGI_FORMAT format,
                                                   bool hdr10,
                                                   float paperWhiteNits)
{
    if (sourceData == nullptr || format != DXGI_FORMAT_R10G10B10A2_UNORM)
    {
        return {};
    }

    std::vector<std::uint8_t> rgba8(static_cast<size_t>(width) * height * 4);
    for (UINT y = 0; y < height; ++y)
    {
        const std::uint32_t* sourceRow =
            reinterpret_cast<const std::uint32_t*>(sourceData + static_cast<size_t>(y) * rowPitch);
        std::uint8_t* destinationRow = rgba8.data() + static_cast<size_t>(y) * width * 4;
        for (UINT x = 0; x < width; ++x)
        {
            const std::uint32_t packed = sourceRow[x];
            float r = static_cast<float>(packed & 0x3ff) / 1023.0f;
            float g = static_cast<float>((packed >> 10) & 0x3ff) / 1023.0f;
            float b = static_cast<float>((packed >> 20) & 0x3ff) / 1023.0f;
            const float a = static_cast<float>((packed >> 30) & 0x3) / 3.0f;

            if (hdr10)
            {
                ConvertHdr10Pixel(r, g, b, paperWhiteNits);
            }

            destinationRow[x * 4 + 0] = ToByte(r);
            destinationRow[x * 4 + 1] = ToByte(g);
            destinationRow[x * 4 + 2] = ToByte(b);
            destinationRow[x * 4 + 3] = ToByte(a);
        }
    }
    return rgba8;
}

bool SaveRgba8Png(
    const std::filesystem::path& path, UINT width, UINT height, const std::uint8_t* rgba8, std::string& error)
{
    error.clear();
    if (path.empty() || width == 0 || height == 0 || rgba8 == nullptr)
    {
        error = "Invalid PNG output arguments.";
        return false;
    }

    const ScopedComInitialization comInitialization;
    if (!comInitialization.IsAvailable())
    {
        error = "COM initialization failed.";
        return false;
    }

    try
    {
        if (path.has_parent_path())
        {
            std::filesystem::create_directories(path.parent_path());
        }
    }
    catch (const std::exception& exception)
    {
        error = exception.what();
        return false;
    }

    Microsoft::WRL::ComPtr<IWICImagingFactory> factory;
    Microsoft::WRL::ComPtr<IWICStream> stream;
    Microsoft::WRL::ComPtr<IWICBitmapEncoder> encoder;
    Microsoft::WRL::ComPtr<IWICBitmapFrameEncode> frame;
    Microsoft::WRL::ComPtr<IPropertyBag2> properties;

    HRESULT result = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    if (SUCCEEDED(result))
        result = factory->CreateStream(&stream);
    if (SUCCEEDED(result))
        result = stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE);
    if (SUCCEEDED(result))
        result = factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder);
    if (SUCCEEDED(result))
        result = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
    if (SUCCEEDED(result))
        result = encoder->CreateNewFrame(&frame, &properties);
    if (SUCCEEDED(result))
        result = frame->Initialize(properties.Get());
    if (SUCCEEDED(result))
        result = frame->SetSize(width, height);

    std::vector<std::uint8_t> bgra8(static_cast<size_t>(width) * height * 4);
    for (size_t pixel = 0; pixel < static_cast<size_t>(width) * height; ++pixel)
    {
        bgra8[pixel * 4 + 0] = rgba8[pixel * 4 + 2];
        bgra8[pixel * 4 + 1] = rgba8[pixel * 4 + 1];
        bgra8[pixel * 4 + 2] = rgba8[pixel * 4 + 0];
        bgra8[pixel * 4 + 3] = rgba8[pixel * 4 + 3];
    }

    WICPixelFormatGUID pixelFormat = GUID_WICPixelFormat32bppBGRA;
    if (SUCCEEDED(result))
        result = frame->SetPixelFormat(&pixelFormat);
    if (SUCCEEDED(result) && pixelFormat != GUID_WICPixelFormat32bppBGRA)
        result = WINCODEC_ERR_UNSUPPORTEDPIXELFORMAT;
    if (SUCCEEDED(result))
        result = frame->WritePixels(height, width * 4, width * height * 4, bgra8.data());
    if (SUCCEEDED(result))
        result = frame->Commit();
    if (SUCCEEDED(result))
        result = encoder->Commit();

    if (FAILED(result))
    {
        error = HResultMessage(result);
        return false;
    }
    return true;
}

bool SaveScreenshotReadback(ScreenshotReadback& readback, const std::filesystem::path& path, std::string& error)
{
    if (!readback.IsValid())
    {
        error = "Screenshot readback is not available.";
        return false;
    }

    std::uint8_t* mappedData = nullptr;
    const D3D12_RANGE readRange = {static_cast<SIZE_T>(readback.layout.Offset),
                                   static_cast<SIZE_T>(readback.layout.Offset) +
                                       static_cast<SIZE_T>(readback.layout.Footprint.RowPitch) * readback.height};
    const HRESULT mapResult = readback.resource->Map(0, &readRange, reinterpret_cast<void**>(&mappedData));
    if (FAILED(mapResult))
    {
        error = HResultMessage(mapResult);
        return false;
    }

    const std::vector<std::uint8_t> rgba8 = ConvertScreenshotToRgba8(mappedData + readback.layout.Offset,
                                                                     readback.width,
                                                                     readback.height,
                                                                     readback.layout.Footprint.RowPitch,
                                                                     readback.format,
                                                                     readback.hdr10,
                                                                     readback.paperWhiteNits);
    const D3D12_RANGE writtenRange = {0, 0};
    readback.resource->Unmap(0, &writtenRange);

    if (rgba8.empty())
    {
        error = "Unsupported screenshot back-buffer format.";
        return false;
    }
    return SaveRgba8Png(path, readback.width, readback.height, rgba8.data(), error);
}
} // namespace Engine
