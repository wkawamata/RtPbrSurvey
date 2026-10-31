#include <windows.h>
#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <DirectXMath.h>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <vector>
#include <string>
#include <filesystem>

using Microsoft::WRL::ComPtr;
using float4x4 = DirectX::XMFLOAT4X4;
using float4 = DirectX::XMFLOAT4;
using float2 = DirectX::XMFLOAT2;
using uint2 = std::array<UINT, 2>;
using int2 = std::array<int, 2>;
using uint = UINT;
#define NRD_CONSTANT(type, name) type name;
#include "RELAX_Config.hlsli"
struct Constants
{
    RELAX_SHARED_CONSTANTS
};
#undef NRD_CONSTANT
static_assert(sizeof(Constants) == 712);
static_assert(offsetof(Constants, gRectSize) == 480);
static_assert(offsetof(Constants, gDenoisingRange) == 588);

namespace
{
constexpr UINT kSize = 16;
void Check(HRESULT result)
{
    if (FAILED(result))
        throw std::runtime_error("D3D12 failure: " + std::to_string(result));
}

ComPtr<ID3D12Resource> Buffer(ID3D12Device* device, UINT64 size, D3D12_HEAP_TYPE type, D3D12_RESOURCE_STATES state)
{
    D3D12_HEAP_PROPERTIES heap = {};
    heap.Type = type;
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = size;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> resource;
    Check(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr, IID_PPV_ARGS(&resource)));
    return resource;
}

ComPtr<ID3D12Resource> Texture(ID3D12Device* device, UINT width, UINT height, DXGI_FORMAT format, bool output)
{
    D3D12_HEAP_PROPERTIES heap = {};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Format = format;
    desc.Flags = output ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS : D3D12_RESOURCE_FLAG_NONE;
    ComPtr<ID3D12Resource> resource;
    Check(
        device->CreateCommittedResource(&heap,
                                        D3D12_HEAP_FLAG_NONE,
                                        &desc,
                                        output ? D3D12_RESOURCE_STATE_UNORDERED_ACCESS : D3D12_RESOURCE_STATE_COPY_DEST,
                                        nullptr,
                                        IID_PPV_ARGS(&resource)));
    return resource;
}

void Transition(ID3D12GraphicsCommandList* commands,
                ID3D12Resource* resource,
                D3D12_RESOURCE_STATES before,
                D3D12_RESOURCE_STATES after)
{
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition = {resource, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, before, after};
    commands->ResourceBarrier(1, &barrier);
}

void Wait(ID3D12Device* device, ID3D12CommandQueue* queue)
{
    ComPtr<ID3D12Fence> fence;
    Check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
    Check(queue->Signal(fence.Get(), 1));
    HANDLE event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!event)
        throw std::runtime_error("Fence event creation failed");
    HRESULT result = fence->SetEventOnCompletion(1, event);
    DWORD wait = SUCCEEDED(result) ? WaitForSingleObject(event, 30000) : WAIT_FAILED;
    CloseHandle(event);
    Check(result);
    if (wait != WAIT_OBJECT_0)
        throw std::runtime_error("Reconstruction timed out");
}

struct ReplayInputs
{
    UINT width;
    UINT height;
    std::filesystem::path folder;
};

void RunCase(ID3D12Device* device,
             ID3D12RootSignature* root,
             ID3D12PipelineState* pipeline,
             UINT caseIndex,
             bool fiveByFive,
             const ReplayInputs* replay = nullptr)
{
    const UINT width = replay ? replay->width : kSize;
    const UINT height = replay ? replay->height : kSize;
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    ComPtr<ID3D12CommandQueue> queue;
    Check(device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&queue)));
    ComPtr<ID3D12CommandAllocator> allocator;
    Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)));
    ComPtr<ID3D12GraphicsCommandList> commands;
    Check(device->CreateCommandList(
        0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), pipeline, IID_PPV_ARGS(&commands)));
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heapDesc.NumDescriptors = 7;
    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ComPtr<ID3D12DescriptorHeap> heap;
    Check(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&heap)));
    heapDesc.NumDescriptors = 2;
    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    ComPtr<ID3D12DescriptorHeap> clearHeap;
    Check(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&clearHeap)));
    const UINT stride = device->GetDescriptorHandleIncrementSize(heapDesc.Type);
    const auto cpu = [&](UINT index)
    {
        auto handle = heap->GetCPUDescriptorHandleForHeapStart();
        handle.ptr += SIZE_T(index) * stride;
        return handle;
    };
    const auto gpu = [&](UINT index)
    {
        auto handle = heap->GetGPUDescriptorHandleForHeapStart();
        handle.ptr += UINT64(index) * stride;
        return handle;
    };
    const auto clearCpu = [&](UINT index)
    {
        auto handle = clearHeap->GetCPUDescriptorHandleForHeapStart();
        handle.ptr += SIZE_T(index) * stride;
        return handle;
    };

    std::array<ComPtr<ID3D12Resource>, 7> textures;
    std::vector<ComPtr<ID3D12Resource>> uploads;
    UINT targetX = caseIndex == 4 ? 0 : 7;
    UINT targetY = caseIndex == 4 ? 0 : 7;
    for (UINT index = 0; index < 7; ++index)
    {
        const UINT textureWidth = index == 0 ? (width + 15) / 16 : width;
        const UINT textureHeight = index == 0 ? (height + 15) / 16 : height;
        const UINT channels = index == 0 || index == 2 ? 1 : 4;
        const DXGI_FORMAT format = channels == 1 ? DXGI_FORMAT_R32_FLOAT : DXGI_FORMAT_R32G32B32A32_FLOAT;
        textures[index] = Texture(device, textureWidth, textureHeight, format, index >= 5);
        if (index >= 5)
        {
            D3D12_UNORDERED_ACCESS_VIEW_DESC view = {};
            view.Format = format;
            view.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
            device->CreateUnorderedAccessView(textures[index].Get(), nullptr, &view, cpu(index));
            device->CreateUnorderedAccessView(textures[index].Get(), nullptr, &view, clearCpu(index - 5));
            continue;
        }
        std::vector<float> data(size_t(textureWidth) * textureHeight * channels, 0);
        if (replay)
        {
            const char* names[] = {"tiles.raw", "normals.raw", "viewz.raw", "specular.raw", "diffuse.raw"};
            std::ifstream input(replay->folder / names[index], std::ios::binary);
            input.read(reinterpret_cast<char*>(data.data()), data.size() * sizeof(float));
            if (!input || input.peek() != std::char_traits<char>::eof())
                throw std::runtime_error("Missing, truncated or oversized replay input");
            for (float value : data)
                if (!std::isfinite(value))
                    throw std::runtime_error("Nonfinite replay input");
        }
        for (UINT y = 0; !replay && y < textureHeight; ++y)
        {
            for (UINT x = 0; x < textureWidth; ++x)
            {
                const UINT offset = (y * textureWidth + x) * channels;
                const bool center = x == targetX && y == targetY;
                if (index == 0)
                    data[offset] = caseIndex == 5 ? 1.f : 0.f;
                else if (index == 1)
                {
                    data[offset + 1] = caseIndex == 8 && !center ? -1.f : 1.f;
                    data[offset + 3] = .5f;
                }
                else if (index == 2)
                    data[offset] = caseIndex == 6 ? 1001.f : (caseIndex == 3 && !center ? 20.f : 10.f);
                else
                {
                    data[offset] = 1;
                    data[offset + 1] = 2;
                    data[offset + 2] = 3;
                    float distance = index == 3 ? 4.f : 2.f;
                    if (caseIndex == 2 || (caseIndex != 0 && center))
                        distance = 0;
                    if (caseIndex == 7)
                        distance = x == 5 && y == 7 ? (index == 3 ? 4.f : 2.f) : 0;
                    data[offset + 3] = distance;
                }
            }
        }
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint;
        UINT64 total = 0;
        const auto desc = textures[index]->GetDesc();
        device->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, nullptr, nullptr, &total);
        uploads.push_back(Buffer(device, total, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ));
        void* mapped = nullptr;
        D3D12_RANGE noRead = {0, 0};
        Check(uploads.back()->Map(0, &noRead, &mapped));
        for (UINT row = 0; row < textureHeight; ++row)
            std::memcpy(static_cast<char*>(mapped) + footprint.Offset + row * footprint.Footprint.RowPitch,
                        data.data() + row * textureWidth * channels,
                        textureWidth * channels * sizeof(float));
        uploads.back()->Unmap(0, nullptr);
        D3D12_TEXTURE_COPY_LOCATION source = {};
        source.pResource = uploads.back().Get();
        source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        source.PlacedFootprint = footprint;
        D3D12_TEXTURE_COPY_LOCATION destination = {};
        destination.pResource = textures[index].Get();
        destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        commands->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
        Transition(commands.Get(),
                   textures[index].Get(),
                   D3D12_RESOURCE_STATE_COPY_DEST,
                   D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        D3D12_SHADER_RESOURCE_VIEW_DESC view = {};
        view.Format = format;
        view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        view.Texture2D.MipLevels = 1;
        device->CreateShaderResourceView(textures[index].Get(), &view, cpu(index));
    }
    Constants constants = {};
    constants.gRectSize = {int(width), int(height)};
    constants.gRectSizeInv = {1.f / width, 1.f / height};
    constants.gDenoisingRange = 1000;
    constants.gViewZScale = 1;
    const auto constantBuffer = Buffer(device, 768, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
    void* mapped = nullptr;
    D3D12_RANGE noRead = {0, 0};
    Check(constantBuffer->Map(0, &noRead, &mapped));
    std::memset(mapped, 0, 768);
    std::memcpy(mapped, &constants, sizeof(constants));
    constantBuffer->Unmap(0, nullptr);
    ID3D12DescriptorHeap* heaps[] = {heap.Get()};
    commands->SetDescriptorHeaps(1, heaps);
    commands->SetComputeRootSignature(root);
    commands->SetComputeRootDescriptorTable(0, gpu(0));
    commands->SetComputeRootDescriptorTable(1, gpu(5));
    commands->SetComputeRootConstantBufferView(2, constantBuffer->GetGPUVirtualAddress());
    const float clear[] = {0, 0, 0, 0};
    for (UINT index = 5; index < 7; ++index)
    {
        commands->ClearUnorderedAccessViewFloat(
            gpu(index), clearCpu(index - 5), textures[index].Get(), clear, 0, nullptr);
        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        barrier.UAV.pResource = textures[index].Get();
        commands->ResourceBarrier(1, &barrier);
    }
    commands->Dispatch((width + 7) / 8, (height + 7) / 8, 1);
    std::array<ComPtr<ID3D12Resource>, 2> readbacks;
    std::array<D3D12_PLACED_SUBRESOURCE_FOOTPRINT, 2> footprints;
    for (UINT lobe = 0; lobe < 2; ++lobe)
    {
        const auto desc = textures[5 + lobe]->GetDesc();
        UINT64 size = 0;
        device->GetCopyableFootprints(&desc, 0, 1, 0, &footprints[lobe], nullptr, nullptr, &size);
        readbacks[lobe] = Buffer(device, size, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
        Transition(commands.Get(),
                   textures[5 + lobe].Get(),
                   D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                   D3D12_RESOURCE_STATE_COPY_SOURCE);
        D3D12_TEXTURE_COPY_LOCATION source = {};
        source.pResource = textures[5 + lobe].Get();
        source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        D3D12_TEXTURE_COPY_LOCATION destination = {};
        destination.pResource = readbacks[lobe].Get();
        destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        destination.PlacedFootprint = footprints[lobe];
        commands->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
    }
    Check(commands->Close());
    ID3D12CommandList* lists[] = {commands.Get()};
    queue->ExecuteCommandLists(1, lists);
    Wait(device, queue.Get());
    for (UINT lobe = 0; lobe < 2; ++lobe)
    {
        Check(readbacks[lobe]->Map(0, nullptr, &mapped));
        if (replay)
        {
            std::vector<float> output(size_t(width) * height * 4);
            for (UINT y = 0; y < height; ++y)
                std::memcpy(output.data() + size_t(y) * width * 4,
                            static_cast<char*>(mapped) + footprints[lobe].Offset +
                                y * footprints[lobe].Footprint.RowPitch,
                            size_t(width) * 4 * sizeof(float));
            readbacks[lobe]->Unmap(0, &noRead);
            for (float value : output)
                if (!std::isfinite(value))
                    throw std::runtime_error("Nonfinite SDK reconstruction output");
            const std::string name =
                std::string(lobe == 0 ? "specular-" : "diffuse-") + (fiveByFive ? "5x5.raw" : "3x3.raw");
            std::ofstream destination(replay->folder / name, std::ios::binary);
            destination.write(reinterpret_cast<const char*>(output.data()), output.size() * sizeof(float));
            if (!destination)
                throw std::runtime_error("Could not write reconstruction replay output");
            continue;
        }
        bool valid = true;
        for (UINT y = 0; y < kSize; ++y)
        {
            const auto* row = reinterpret_cast<const float*>(static_cast<char*>(mapped) + footprints[lobe].Offset +
                                                             y * footprints[lobe].Footprint.RowPitch);
            for (UINT x = 0; x < kSize; ++x)
            {
                for (UINT channel = 0; channel < 4; ++channel)
                    valid &= std::isfinite(row[4 * x + channel]);
                const bool excluded = caseIndex == 5 || caseIndex == 6;
                for (UINT channel = 0; channel < 3; ++channel)
                    valid &= std::abs(row[4 * x + channel] - (excluded ? 0.f : float(channel + 1))) < 1e-5f;
                bool missing = excluded || caseIndex == 2 || (caseIndex == 3 && x == targetX && y == targetY);
                if (caseIndex == 7)
                {
                    const int radius = fiveByFive ? 2 : 1;
                    missing = std::abs(int(x) - 5) > radius || std::abs(int(y) - 7) > radius;
                }
                const float expectedDistance = missing ? 0.f : (lobe == 0 ? 4.f : 2.f);
                valid &= std::abs(row[4 * x + 3] - expectedDistance) < .0005f;
            }
        }
        const auto* target = reinterpret_cast<const float*>(static_cast<char*>(mapped) + footprints[lobe].Offset +
                                                            targetY * footprints[lobe].Footprint.RowPitch) +
                             targetX * 4;
        const bool unresolved =
            caseIndex == 2 || caseIndex == 3 || caseIndex == 5 || caseIndex == 6 || (caseIndex == 7 && !fiveByFive);
        const float expected = unresolved ? 0.f : (lobe == 0 ? 4.f : 2.f);
        valid &= std::abs(target[3] - expected) < .0005f;
        readbacks[lobe]->Unmap(0, &noRead);
        if (!valid)
            throw std::runtime_error("Reconstruction case failed: " + std::to_string(caseIndex));
    }
    std::cout << "case " << caseIndex << " passed\n";
}
} // namespace

int main(int argc, char** argv)
{
    try
    {
        const bool replayMode = argc >= 4 && std::strcmp(argv[3], "--replay") == 0;
        if ((!replayMode && (argc < 3 || argc > 4)) || (replayMode && argc != 7 && argc != 8))
            throw std::runtime_error("Expected shader, 3x3/5x5, optional --replay folder width height and --hardware");
        const bool five = std::strcmp(argv[2], "5x5") == 0;
        if (!five && std::strcmp(argv[2], "3x3") != 0)
            throw std::runtime_error("Invalid neighborhood mode");
        const bool hardware = (argc == 4 || argc == 8) && std::strcmp(argv[argc - 1], "--hardware") == 0;
        if ((argc == 4 || argc == 8) && !hardware)
            throw std::runtime_error("Unknown option");
        ReplayInputs replay = {};
        if (replayMode)
        {
            replay.folder = argv[4];
            const unsigned long width = std::stoul(argv[5]);
            const unsigned long height = std::stoul(argv[6]);
            if (width == 0 || height == 0 || width > 8192 || height > 8192)
                throw std::runtime_error("Invalid replay dimensions");
            replay.width = UINT(width);
            replay.height = UINT(height);
        }
        std::ifstream stream(argv[1], std::ios::binary);
        std::vector<char> shader((std::istreambuf_iterator<char>(stream)), {});
        if (shader.empty())
            throw std::runtime_error("Missing shader");
        ComPtr<ID3D12Debug> debug;
        Check(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)));
        debug->EnableDebugLayer();
        ComPtr<IDXGIFactory4> factory;
        Check(CreateDXGIFactory2(DXGI_CREATE_FACTORY_DEBUG, IID_PPV_ARGS(&factory)));
        ComPtr<IDXGIAdapter> adapter;
        if (!hardware)
            Check(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)));
        ComPtr<ID3D12Device> device;
        Check(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)));
        ComPtr<IDXGIAdapter> selectedAdapter;
        Check(factory->EnumAdapterByLuid(device->GetAdapterLuid(), IID_PPV_ARGS(&selectedAdapter)));
        DXGI_ADAPTER_DESC adapterDesc = {};
        Check(selectedAdapter->GetDesc(&adapterDesc));
        std::wcout << L"Adapter: " << adapterDesc.Description << L'\n';
        ComPtr<ID3D12InfoQueue> info;
        Check(device.As(&info));
        D3D12_DESCRIPTOR_RANGE ranges[2] = {};
        ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        ranges[0].NumDescriptors = 5;
        ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        ranges[1].NumDescriptors = 2;
        D3D12_ROOT_PARAMETER parameters[3] = {};
        for (UINT index = 0; index < 2; ++index)
        {
            parameters[index].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            parameters[index].DescriptorTable = {1, &ranges[index]};
        }
        parameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        parameters[2].Descriptor.RegisterSpace = 1;
        D3D12_ROOT_SIGNATURE_DESC desc = {};
        desc.NumParameters = 3;
        desc.pParameters = parameters;
        ComPtr<ID3DBlob> blob, errors;
        Check(D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &errors));
        ComPtr<ID3D12RootSignature> root;
        Check(device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&root)));
        D3D12_COMPUTE_PIPELINE_STATE_DESC pipelineDesc = {};
        pipelineDesc.pRootSignature = root.Get();
        pipelineDesc.CS = {shader.data(), shader.size()};
        ComPtr<ID3D12PipelineState> pipeline;
        Check(device->CreateComputePipelineState(&pipelineDesc, IID_PPV_ARGS(&pipeline)));
        if (replayMode)
            RunCase(device.Get(), root.Get(), pipeline.Get(), 0, five, &replay);
        else
            for (UINT index = 0; index < 9; ++index)
                RunCase(device.Get(), root.Get(), pipeline.Get(), index, five);
        for (UINT64 index = 0; index < info->GetNumStoredMessages(); ++index)
        {
            SIZE_T size = 0;
            Check(info->GetMessage(index, nullptr, &size));
            std::vector<char> bytes(size);
            auto* message = reinterpret_cast<D3D12_MESSAGE*>(bytes.data());
            Check(info->GetMessage(index, message, &size));
            if (message->Severity <= D3D12_MESSAGE_SEVERITY_ERROR)
                throw std::runtime_error(message->pDescription);
        }
        std::cout << (hardware ? "Hardware" : "WARP") << (replayMode ? ": replay" : ": 9 cases")
                  << ", no D3D12 errors.\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
