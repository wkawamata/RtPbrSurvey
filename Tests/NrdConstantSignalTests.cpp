#include "Renderer/NrdRelaxD3D12.h"
#include <windows.h>
#include <d3d12sdklayers.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <DirectXMath.h>
#include <DirectXPackedVector.h>
#include <nlohmann/json.hpp>
#include <array>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace
{
constexpr UINT kSize = 64;
using Color = std::array<float, 4>;

struct NoiseCase
{
    UINT seed;
    float amplitude;
};

UINT NoiseHash(UINT value)
{
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    return value ^ (value >> 16);
}

std::vector<Color> Signal(const Color& base, const NoiseCase& noise, UINT frame, UINT lobe)
{
    std::vector<Color> values(kSize*kSize, base);
    for (UINT y = 0; y < kSize; ++y)
        for (UINT x = 0; x < kSize; ++x)
            for (UINT c = 0; c < 3; ++c)
            {
                const UINT key = noise.seed ^ ((y*(kSize/2)+x/2)*0x9e3779b9u) ^
                                 (frame*0x85ebca6bu) ^ (lobe*0xc2b2ae35u) ^ (c*0x27d4eb2fu);
                const bool positive = ((NoiseHash(key) & 1u) != 0) != ((x & 1u) != 0);
                values[y*kSize+x][c] *= 1.0f + (positive ? noise.amplitude : -noise.amplitude);
            }
    return values;
}

void Check(HRESULT result)
{
    if (FAILED(result))
        throw std::runtime_error("D3D12 failure: " + std::to_string(result));
}

ComPtr<ID3D12Resource> Buffer(ID3D12Device* device, UINT64 size, D3D12_HEAP_TYPE type,
                              D3D12_RESOURCE_STATES state)
{
    D3D12_HEAP_PROPERTIES heap = {};
    heap.Type = type;
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = size;
    desc.Height = desc.DepthOrArraySize = desc.MipLevels = desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> resource;
    Check(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr,
                                          IID_PPV_ARGS(&resource)));
    return resource;
}

void Transition(ID3D12GraphicsCommandList* commands, ID3D12Resource* resource,
                D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
{
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition = {resource, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, before, after};
    commands->ResourceBarrier(1, &barrier);
}

struct Gpu
{
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> commands;

    void Begin()
    {
        Check(allocator->Reset());
        Check(commands->Reset(allocator.Get(), nullptr));
    }

    void Submit()
    {
        Check(commands->Close());
        ID3D12CommandList* lists[] = {commands.Get()};
        queue->ExecuteCommandLists(1, lists);
        ComPtr<ID3D12Fence> fence;
        Check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
        Check(queue->Signal(fence.Get(), 1));
        HANDLE event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        if (!event)
            throw std::runtime_error("Fence event creation failed");
        HRESULT result = fence->SetEventOnCompletion(1, event);
        DWORD waited = SUCCEEDED(result) ? WaitForSingleObject(event, 30000) : WAIT_FAILED;
        CloseHandle(event);
        Check(result);
        if (waited != WAIT_OBJECT_0)
            throw std::runtime_error("GPU timeout");
        Check(device->GetDeviceRemovedReason());
    }
};

ComPtr<ID3D12Resource> Texture(Gpu& gpu, UINT index, const Color& value,
                               std::vector<ComPtr<ID3D12Resource>>& uploads)
{
    const bool output = index >= 5;
    const DXGI_FORMAT format = output ? DXGI_FORMAT_R16G16B16A16_FLOAT
                                     : index == 2 ? DXGI_FORMAT_R32_FLOAT : DXGI_FORMAT_R32G32B32A32_FLOAT;
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = desc.Height = kSize;
    desc.DepthOrArraySize = desc.MipLevels = desc.SampleDesc.Count = 1;
    desc.Format = format;
    desc.Flags = output ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS : D3D12_RESOURCE_FLAG_NONE;
    D3D12_HEAP_PROPERTIES heap = {};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    ComPtr<ID3D12Resource> texture;
    Check(gpu.device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&texture)));
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint;
    UINT64 bytes = 0;
    gpu.device->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, nullptr, nullptr, &bytes);
    uploads.push_back(Buffer(gpu.device.Get(), bytes, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ));
    void* mapped = nullptr;
    D3D12_RANGE empty = {0, 0};
    Check(uploads.back()->Map(0, &empty, &mapped));
    std::memset(mapped, 0, static_cast<size_t>(bytes));
    for (UINT y = 0; y < kSize; ++y)
    {
        auto* row = static_cast<char*>(mapped) + footprint.Offset + y*footprint.Footprint.RowPitch;
        if (!output)
            for (UINT x = 0; x < kSize; ++x)
                std::memcpy(row + x*(index == 2 ? 4 : 16), value.data(), index == 2 ? 4 : 16);
    }
    uploads.back()->Unmap(0, nullptr);
    D3D12_TEXTURE_COPY_LOCATION source = {}, destination = {};
    source.pResource = uploads.back().Get();
    source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    source.PlacedFootprint = footprint;
    destination.pResource = texture.Get();
    destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    gpu.commands->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
    Transition(gpu.commands.Get(), texture.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
        output ? D3D12_RESOURCE_STATE_UNORDERED_ACCESS : D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    return texture;
}

void UploadSignal(Gpu& gpu, ID3D12Resource* texture, const std::vector<Color>& values,
                   std::vector<ComPtr<ID3D12Resource>>& uploads)
{
    const auto desc = texture->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint;
    UINT64 bytes = 0;
    gpu.device->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, nullptr, nullptr, &bytes);
    uploads.push_back(Buffer(gpu.device.Get(), bytes, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ));
    void* mapped = nullptr;
    D3D12_RANGE empty = {0, 0};
    Check(uploads.back()->Map(0, &empty, &mapped));
    for (UINT y = 0; y < kSize; ++y)
        std::memcpy(static_cast<char*>(mapped) + footprint.Offset + y*footprint.Footprint.RowPitch,
                    values.data()+y*kSize, kSize*sizeof(Color));
    uploads.back()->Unmap(0, nullptr);
    Transition(gpu.commands.Get(), texture, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                D3D12_RESOURCE_STATE_COPY_DEST);
    D3D12_TEXTURE_COPY_LOCATION source = {}, destination = {};
    source.pResource = uploads.back().Get();
    source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    source.PlacedFootprint = footprint;
    destination.pResource = texture;
    destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    gpu.commands->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
    Transition(gpu.commands.Get(), texture, D3D12_RESOURCE_STATE_COPY_DEST,
                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
}

nlohmann::json Read(Gpu& gpu, ID3D12Resource* texture, const Color& expected,
                    D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
{
    const auto desc = texture->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint;
    UINT64 bytes = 0;
    gpu.device->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, nullptr, nullptr, &bytes);
    auto readback = Buffer(gpu.device.Get(), bytes, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
    gpu.Begin();
    Transition(gpu.commands.Get(), texture, state, D3D12_RESOURCE_STATE_COPY_SOURCE);
    D3D12_TEXTURE_COPY_LOCATION source = {}, destination = {};
    source.pResource = texture;
    source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    destination.pResource = readback.Get();
    destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    destination.PlacedFootprint = footprint;
    gpu.commands->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
    Transition(gpu.commands.Get(), texture, D3D12_RESOURCE_STATE_COPY_SOURCE, state);
    gpu.Submit();
    void* mapped = nullptr;
    D3D12_RANGE range = {0, static_cast<SIZE_T>(bytes)};
    Check(readback->Map(0, &range, &mapped));
    std::array<double, 3> mean = {}, maximum = {};
    std::vector<float> samples;
    samples.reserve(kSize*kSize*3);
    bool passed = true;
    UINT nonfinite = 0;
    for (UINT y = 0; y < kSize; ++y)
    {
        const auto* row = static_cast<const char*>(mapped) + footprint.Offset + y*footprint.Footprint.RowPitch;
        for (UINT x = 0; x < kSize; ++x)
            for (UINT c = 0; c < 3; ++c)
            {
                const float value = desc.Format == DXGI_FORMAT_R32G32B32A32_FLOAT
                    ? reinterpret_cast<const float*>(row)[x*4+c]
                    : DirectX::PackedVector::XMConvertHalfToFloat(reinterpret_cast<const uint16_t*>(row)[x*4+c]);
                samples.push_back(value);
                if (!std::isfinite(value))
                {
                    ++nonfinite;
                    passed = false;
                    continue;
                }
                mean[c] += value / double(kSize*kSize);
                const double error = std::abs(double(value)-expected[c]);
                maximum[c] = (std::max)(maximum[c], error);
                const double bound = expected[c] == 0 ? 1e-6 : .004*(std::max)(1.0, double(expected[c]));
                passed &= value >= 0 && error <= bound;
            }
    }
    D3D12_RANGE noWrite = {0, 0};
    readback->Unmap(0, &noWrite);
    return {{"expectedRgb", {expected[0], expected[1], expected[2]}}, {"meanRgb", mean},
            {"maximumAbsoluteErrorRgb", maximum}, {"nonfiniteValues", nonfinite}, {"passed", passed},
            {"rgbSamples", samples}};
}

void Run(Gpu& gpu, const std::filesystem::path& shaders, UINT caseIndex, nlohmann::json& report,
          const NoiseCase* noise = nullptr)
{
    Color diffuse = {0, 0, 0, 2}, specular = diffuse;
    if (caseIndex == 1 || caseIndex == 3)
        diffuse = {.125f, .5f, 2, 2};
    if (caseIndex == 2 || caseIndex == 3)
        specular = {2, .25f, .125f, 2};
    const std::array<Color, 7> values = {Color{0, 0, 0, 0}, Color{0, 0, -1, .5f}, Color{5, 0, 0, 0},
                                       diffuse, specular, Color{}, Color{}};
    std::array<ComPtr<ID3D12Resource>, 7> textures;
    std::vector<ComPtr<ID3D12Resource>> uploads;
    gpu.Begin();
    for (UINT index = 0; index < 7; ++index)
        textures[index] = Texture(gpu, index, values[index], uploads);
    gpu.Submit();
    Engine::NrdRelaxD3D12 backend;
    backend.Initialize(gpu.device.Get(), shaders, 1);
    Engine::NrdD3D12Inputs inputs = {textures[0].Get(), textures[1].Get(), textures[2].Get(), textures[3].Get(),
                                    textures[4].Get(), textures[5].Get(), textures[6].Get()};
    Engine::NrdSdkFrame frame;
    frame.width = frame.height = frame.previousWidth = frame.previousHeight = kSize;
    DirectX::XMFLOAT4X4 view, projection;
    DirectX::XMStoreFloat4x4(&view, DirectX::XMMatrixIdentity());
    DirectX::XMStoreFloat4x4(&projection, DirectX::XMMatrixPerspectiveFovLH(1, 1, .1f, 1000));
    std::memcpy(frame.worldToView.data(), &view, sizeof(view));
    std::memcpy(frame.viewToClip.data(), &projection, sizeof(projection));
    frame.previousWorldToView = frame.worldToView;
    frame.previousViewToClip = frame.viewToClip;
    std::array<std::vector<double>, 2> sums;
    for (auto& sum : sums)
        sum.resize(kSize*kSize*3, 0);
    for (UINT index = 0; index < 120; ++index)
    {
        frame.frameIndex = index;
        frame.reset = index == 0;
        gpu.Begin();
        uploads.clear();
        if (noise != nullptr)
        {
            for (UINT lobe = 0; lobe < 2; ++lobe)
            {
                const auto values = Signal(lobe == 0 ? diffuse : specular, *noise, index, lobe);
                UploadSignal(gpu, textures[3+lobe].Get(), values, uploads);
                for (UINT pixel = 0; pixel < kSize*kSize; ++pixel)
                    for (UINT c = 0; c < 3; ++c)
                        sums[lobe][pixel*3+c] += values[pixel][c];
            }
        }
        const UINT dispatches = backend.Record(gpu.commands.Get(), 0, frame, inputs);
        gpu.Submit();
        if (index == 0 || index == 29 || index == 119)
        {
            report["checkpoints"].push_back({{"case", caseIndex}, {"elapsedFrames", index+1},
                {"sdkFrameIndex", index}, {"reset", frame.reset}, {"dispatchCount", dispatches},
                {"diffuse", Read(gpu, textures[5].Get(), diffuse)},
                {"specular", Read(gpu, textures[6].Get(), specular)}});
            if (noise != nullptr)
            {
                auto& row = report["checkpoints"].back();
                row["seed"] = noise->seed;
                row["amplitude"] = noise->amplitude;
                row["inputDiffuse"] = Read(gpu, textures[3].Get(), diffuse, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                row["inputSpecular"] = Read(gpu, textures[4].Get(), specular, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                for (UINT lobe = 0; lobe < 2; ++lobe)
                {
                    auto average = sums[lobe];
                    for (double& value : average)
                        value /= index+1;
                    row[lobe == 0 ? "temporalNativeDiffuse" : "temporalNativeSpecular"] = average;
                }
            }
        }
    }
}
} // namespace

int main(int argc, char** argv)
{
    nlohmann::json report = {{"schemaVersion", 1}, {"status", "running"}, {"checkpoints", nlohmann::json::array()},
        {"failures", nlohmann::json::array()}, {"fullIntegrationReady", false}, {"width", kSize}, {"height", kSize},
        {"sdkVersion", "4.17.3"}, {"prepass", {30, 50}}, {"normalEncoding", 4}, {"roughnessEncoding", 1},
        {"inputFormat", "RGBA32_FLOAT / R32_FLOAT viewZ"}, {"outputFormat", "RGBA16_FLOAT"},
        {"bound", "zero: 1e-6; nonzero: 0.004 * max(1, expected channel)"},
        {"limitation", "Predeclared constant-field health check, not an SDK guarantee or scene quality bound."}};
    try
    {
        const bool noisy = argc == 4 && std::strcmp(argv[3], "--noise") == 0;
        if ((argc != 3 && !noisy) || std::filesystem::exists(argv[2]))
            throw std::runtime_error("Expected shader directory and new report path");
        if (noisy)
        {
            report["mode"] = "balanced-pair-noise";
            report["seeds"] = {7, 101, 307};
            report["amplitudes"] = {0, .25, .75};
            report["limitation"] = "Paired multiplicative Rademacher noise; spatially balanced, not independent white/PT noise.";
        }
        ComPtr<ID3D12Debug> debug;
        Check(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)));
        debug->EnableDebugLayer();
        Gpu gpu;
        Check(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&gpu.device)));
        ComPtr<IDXGIFactory4> factory;
        Check(CreateDXGIFactory2(DXGI_CREATE_FACTORY_DEBUG, IID_PPV_ARGS(&factory)));
        ComPtr<IDXGIAdapter> adapter;
        Check(factory->EnumAdapterByLuid(gpu.device->GetAdapterLuid(), IID_PPV_ARGS(&adapter)));
        DXGI_ADAPTER_DESC adapterDesc;
        Check(adapter->GetDesc(&adapterDesc));
        if ((adapterDesc.VendorId == 0x1414 && adapterDesc.DeviceId == 0x8c))
            throw std::runtime_error("Hardware GPU required");
        report["adapter"] = {{"vendorId", adapterDesc.VendorId}, {"deviceId", adapterDesc.DeviceId}};
        D3D12_COMMAND_QUEUE_DESC queueDesc = {};
        Check(gpu.device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&gpu.queue)));
        Check(gpu.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&gpu.allocator)));
        Check(gpu.device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, gpu.allocator.Get(), nullptr,
                                           IID_PPV_ARGS(&gpu.commands)));
        Check(gpu.commands->Close());
        ComPtr<ID3D12InfoQueue> info;
        Check(gpu.device.As(&info));
        if (noisy)
        {
            for (UINT seed : {7u, 101u, 307u})
                for (float amplitude : {0.f, .25f, .75f})
                {
                    const NoiseCase noise = {seed, amplitude};
                    Run(gpu, argv[1], 3, report, &noise);
                }
        }
        else
        {
            for (UINT caseIndex = 0; caseIndex < 4; ++caseIndex)
                Run(gpu, argv[1], caseIndex, report);
        }
        UINT errors = 0;
        for (UINT64 index = 0; index < info->GetNumStoredMessages(); ++index)
        {
            SIZE_T size = 0;
            Check(info->GetMessage(index, nullptr, &size));
            std::vector<char> bytes(size);
            auto* message = reinterpret_cast<D3D12_MESSAGE*>(bytes.data());
            Check(info->GetMessage(index, message, &size));
            if (message->Severity <= D3D12_MESSAGE_SEVERITY_ERROR)
            {
                ++errors;
                report["failures"].push_back(message->pDescription);
            }
        }
        report["d3d12Errors"] = errors;
        bool passed = errors == 0 && report["checkpoints"].size() == (noisy ? 27 : 12);
        for (const auto& row : report["checkpoints"])
        {
            passed &= row["dispatchCount"].get<UINT>() > 0;
            for (const char* lobe : {"diffuse", "specular"})
                passed &= noisy ? row[lobe]["nonfiniteValues"].get<UINT>() == 0 : row[lobe]["passed"].get<bool>();
        }
        report["status"] = noisy ? (passed ? "completed-noise-controls" : "failed-noise-controls")
                                  : (passed ? "passed-constant-signal-controls" : "failed-constant-signal-controls");
    }
    catch (const std::exception& error)
    {
        report["status"] = "failed-execution";
        report["failures"].push_back(error.what());
    }
    if (argc >= 3 && !std::filesystem::exists(argv[2]))
    {
        std::ofstream stream(argv[2]);
        stream << report.dump(2) << '\n';
        if (!stream)
            return 2;
    }
    std::cout << report["status"] << '\n';
    return report["status"] != "passed-constant-signal-controls" && report["status"] != "completed-noise-controls";
}
