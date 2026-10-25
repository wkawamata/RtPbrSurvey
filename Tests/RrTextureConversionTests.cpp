#include <windows.h>
#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <vector>
#include <string>
#ifdef RTPBRSURVEY_RR_NATIVE_REPLAY
#include "Renderer/StreamlineAdapter.h"
#include "Renderer/PathTracingRrHistory.h"
#include <nlohmann/json.hpp>
#include <cmath>
#endif

using Microsoft::WRL::ComPtr;
namespace
{
void Check(HRESULT result)
{
    if (FAILED(result))
        throw std::runtime_error("D3D12 operation failed: " + std::to_string(result));
}

std::vector<char> Read(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
        throw std::runtime_error("Missing input: " + path.string());
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

ComPtr<ID3D12Resource> Buffer(ID3D12Device* device, UINT64 size, D3D12_HEAP_TYPE type, D3D12_RESOURCE_STATES state)
{
    D3D12_HEAP_PROPERTIES heap = {};
    heap.Type = type;
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = size;
    desc.Height = desc.DepthOrArraySize = desc.MipLevels = desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> resource;
    Check(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr, IID_PPV_ARGS(&resource)));
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

struct Frame
{
    std::array<float, 16> inverseViewProjection;
    UINT width;
    UINT height;
};
static_assert(sizeof(Frame) == 72);
#ifdef RTPBRSURVEY_RR_NATIVE_REPLAY
#include "RrNativeReplay.h"
#endif
} // namespace

int main(int argc, char** argv)
{
    try
    {
        if (argc != 4 && argc != 5)
            throw std::runtime_error("Expected shader, input folder, output folder, optional --hardware");
        const bool hardware = argc == 5 && std::string(argv[4]) == "--hardware";
        if (argc == 5 && !hardware)
            throw std::runtime_error("Unknown option");
        const std::filesystem::path input = std::filesystem::canonical(argv[2]);
        const std::filesystem::path output = std::filesystem::absolute(argv[3]);
        std::filesystem::create_directories(output);
        for (UINT index = 0; index < 9; ++index)
        {
            if (std::filesystem::exists(output / ("out" + std::to_string(index) + ".bin")))
                throw std::runtime_error("Output already exists");
        }
        const auto bytes = Read(input / "frame.bin");
        Frame frame;
        if (bytes.size() != sizeof(frame))
            throw std::runtime_error("Invalid frame constants");
        std::memcpy(&frame, bytes.data(), sizeof(frame));
        if (frame.width == 0 || frame.height == 0 || frame.width > 16384 || frame.height > 16384 ||
            UINT64(frame.width) * frame.height > 4194304)
            throw std::runtime_error("Unsupported replay dimensions");
        const auto shader = Read(argv[1]);
#ifdef RTPBRSURVEY_RR_NATIVE_REPLAY
        if (!hardware)
            throw std::runtime_error("Native RR replay requires --hardware");
        Engine::InitializeStreamlineAdapter({L"PT RR replay", nullptr, "1.0.0"});
#endif
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
        ComPtr<ID3D12InfoQueue> info;
        Check(device.As(&info));
#ifdef RTPBRSURVEY_RR_NATIVE_REPLAY
        Engine::SetStreamlineD3DDevice(device.Get());
        struct StreamlineShutdown
        {
            ~StreamlineShutdown()
            {
                Engine::ShutdownStreamlineAdapter();
            }
        } shutdown;
        const auto support = Engine::QueryStreamlineRayReconstructionDiagnostics();
        std::cout << "RR support: " << support.StatusText() << " / " << support.supportQueryResultName << std::endl;
        if (support.status != Engine::RayReconstructionSupportStatus::Available)
            throw std::runtime_error("Native RR feature unavailable");
#endif

        D3D12_DESCRIPTOR_RANGE ranges[2] = {};
        ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        ranges[0].NumDescriptors = 8;
        ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        ranges[1].NumDescriptors = 9;
        D3D12_ROOT_PARAMETER parameters[3] = {};
        for (UINT index = 0; index < 2; ++index)
        {
            parameters[index].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            parameters[index].DescriptorTable = {1, &ranges[index]};
        }
        parameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        parameters[2].Constants.Num32BitValues = sizeof(Frame) / 4;
        D3D12_ROOT_SIGNATURE_DESC rootDesc = {};
        rootDesc.NumParameters = 3;
        rootDesc.pParameters = parameters;
        ComPtr<ID3DBlob> blob, errors;
        Check(D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &errors));
        ComPtr<ID3D12RootSignature> root;
        Check(device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&root)));
        D3D12_COMPUTE_PIPELINE_STATE_DESC pipelineDesc = {};
        pipelineDesc.pRootSignature = root.Get();
        pipelineDesc.CS = {shader.data(), shader.size()};
        ComPtr<ID3D12PipelineState> pipeline;
        Check(device->CreateComputePipelineState(&pipelineDesc, IID_PPV_ARGS(&pipeline)));
        D3D12_COMMAND_QUEUE_DESC queueDesc = {};
        ComPtr<ID3D12CommandQueue> queue;
        Check(device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&queue)));
        ComPtr<ID3D12CommandAllocator> allocator;
        Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)));
        ComPtr<ID3D12GraphicsCommandList> commands;
        Check(device->CreateCommandList(
            0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), pipeline.Get(), IID_PPV_ARGS(&commands)));
        D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
        heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        heapDesc.NumDescriptors = 17;
        heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        ComPtr<ID3D12DescriptorHeap> descriptors;
        Check(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&descriptors)));
        const UINT stride = device->GetDescriptorHandleIncrementSize(heapDesc.Type);
        const auto cpu = [&](UINT index)
        {
            auto handle = descriptors->GetCPUDescriptorHandleForHeapStart();
            handle.ptr += SIZE_T(index) * stride;
            return handle;
        };
        const auto gpu = [&](UINT index)
        {
            auto handle = descriptors->GetGPUDescriptorHandleForHeapStart();
            handle.ptr += UINT64(index) * stride;
            return handle;
        };
        std::array<ComPtr<ID3D12Resource>, 17> textures;
        std::array<ComPtr<ID3D12Resource>, 8> uploads;
        std::array<ComPtr<ID3D12Resource>, 9> readbacks;
        std::array<D3D12_PLACED_SUBRESOURCE_FOOTPRINT, 17> footprints;
        std::array<UINT64, 17> sizes;
        std::array<UINT64, 17> rowBytes;
        for (UINT index = 0; index < 17; ++index)
        {
            const bool destination = index >= 8;
            const UINT slot = destination ? index - 8 : index;
            const UINT channels = destination ? (slot < 4    ? 4
                                                 : slot == 6 ? 2
                                                             : 1)
                                              : (slot == 2   ? 1
                                                 : slot == 3 ? 2
                                                             : 4);
            D3D12_HEAP_PROPERTIES heap = {};
            heap.Type = D3D12_HEAP_TYPE_DEFAULT;
            D3D12_RESOURCE_DESC desc = {};
            desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            desc.Width = frame.width;
            desc.Height = frame.height;
            desc.DepthOrArraySize = desc.MipLevels = desc.SampleDesc.Count = 1;
            desc.Format = destination && slot == 8 ? DXGI_FORMAT_R32_UINT
                          : channels == 1          ? DXGI_FORMAT_R32_FLOAT
                          : channels == 2          ? DXGI_FORMAT_R32G32_FLOAT
                                                   : DXGI_FORMAT_R32G32B32A32_FLOAT;
            desc.Flags = destination ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS : D3D12_RESOURCE_FLAG_NONE;
            Check(device->CreateCommittedResource(&heap,
                                                  D3D12_HEAP_FLAG_NONE,
                                                  &desc,
                                                  destination ? D3D12_RESOURCE_STATE_UNORDERED_ACCESS
                                                              : D3D12_RESOURCE_STATE_COPY_DEST,
                                                  nullptr,
                                                  IID_PPV_ARGS(&textures[index])));
            device->GetCopyableFootprints(&desc, 0, 1, 0, &footprints[index], nullptr, &rowBytes[index], &sizes[index]);
            if (destination)
            {
                D3D12_UNORDERED_ACCESS_VIEW_DESC view = {};
                view.Format = desc.Format;
                view.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
                device->CreateUnorderedAccessView(textures[index].Get(), nullptr, &view, cpu(index));
                readbacks[slot] =
                    Buffer(device.Get(), sizes[index], D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
            }
            else
            {
                const auto raw = Read(input / ("in" + std::to_string(slot) + ".bin"));
                if (raw.size() != rowBytes[index] * frame.height)
                    throw std::runtime_error("Invalid input texture size");
                uploads[slot] =
                    Buffer(device.Get(), sizes[index], D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
                void* mapped;
                const D3D12_RANGE noRead = {0, 0};
                Check(uploads[slot]->Map(0, &noRead, &mapped));
                for (UINT row = 0; row < frame.height; ++row)
                    std::memcpy(static_cast<char*>(mapped) + row * footprints[index].Footprint.RowPitch,
                                raw.data() + row * rowBytes[index],
                                SIZE_T(rowBytes[index]));
                uploads[slot]->Unmap(0, nullptr);
                D3D12_TEXTURE_COPY_LOCATION source = {};
                source.pResource = uploads[slot].Get();
                source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
                source.PlacedFootprint = footprints[index];
                D3D12_TEXTURE_COPY_LOCATION target = {};
                target.pResource = textures[index].Get();
                commands->CopyTextureRegion(&target, 0, 0, 0, &source, nullptr);
                Transition(commands.Get(),
                           textures[index].Get(),
                           D3D12_RESOURCE_STATE_COPY_DEST,
                           D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                D3D12_SHADER_RESOURCE_VIEW_DESC view = {};
                view.Format = desc.Format;
                view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                view.Texture2D.MipLevels = 1;
                device->CreateShaderResourceView(textures[index].Get(), &view, cpu(index));
            }
        }
        ID3D12DescriptorHeap* heaps[] = {descriptors.Get()};
        commands->SetDescriptorHeaps(1, heaps);
        commands->SetComputeRootSignature(root.Get());
        commands->SetComputeRootDescriptorTable(0, gpu(0));
        commands->SetComputeRootDescriptorTable(1, gpu(8));
        commands->SetComputeRoot32BitConstants(2, sizeof(Frame) / 4, &frame, 0);
        commands->Dispatch((frame.width + 7) / 8, (frame.height + 7) / 8, 1);
        for (UINT slot = 0; slot < 9; ++slot)
        {
            const UINT index = slot + 8;
            Transition(commands.Get(),
                       textures[index].Get(),
                       D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                       D3D12_RESOURCE_STATE_COPY_SOURCE);
            D3D12_TEXTURE_COPY_LOCATION source = {};
            source.pResource = textures[index].Get();
            D3D12_TEXTURE_COPY_LOCATION target = {};
            target.pResource = readbacks[slot].Get();
            target.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            target.PlacedFootprint = footprints[index];
            commands->CopyTextureRegion(&target, 0, 0, 0, &source, nullptr);
        }
        Check(commands->Close());
        ID3D12CommandList* lists[] = {commands.Get()};
        queue->ExecuteCommandLists(1, lists);
        ComPtr<ID3D12Fence> fence;
        Check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
        Check(queue->Signal(fence.Get(), 1));
        HANDLE event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        if (!event)
            throw std::runtime_error("Fence event creation failed");
        const HRESULT result = fence->SetEventOnCompletion(1, event);
        const DWORD wait = SUCCEEDED(result) ? WaitForSingleObject(event, 30000) : WAIT_FAILED;
        CloseHandle(event);
        Check(result);
        if (wait != WAIT_OBJECT_0)
            throw std::runtime_error("GPU replay timed out");
        for (UINT64 index = 0; index < info->GetNumStoredMessages(); ++index)
        {
            SIZE_T size = 0;
            Check(info->GetMessage(index, nullptr, &size));
            std::vector<char> storage(size);
            auto* message = reinterpret_cast<D3D12_MESSAGE*>(storage.data());
            Check(info->GetMessage(index, message, &size));
            if (message->Severity <= D3D12_MESSAGE_SEVERITY_ERROR)
                throw std::runtime_error(message->pDescription);
        }
        for (UINT slot = 0; slot < 9; ++slot)
        {
            const UINT index = slot + 8;
            void* mapped;
            const D3D12_RANGE range = {0, SIZE_T(sizes[index])};
            Check(readbacks[slot]->Map(0, &range, &mapped));
            std::ofstream stream(output / ("out" + std::to_string(slot) + ".bin"), std::ios::binary);
            for (UINT row = 0; row < frame.height; ++row)
                stream.write(static_cast<char*>(mapped) + row * footprints[index].Footprint.RowPitch,
                             std::streamsize(rowBytes[index]));
            if (!stream)
                throw std::runtime_error("Output write failed");
            const D3D12_RANGE noWrite = {0, 0};
            readbacks[slot]->Unmap(0, &noWrite);
        }
        std::cout << (hardware ? "Hardware" : "WARP") << ": texture conversion completed; D3D12 errors 0\n";
#ifdef RTPBRSURVEY_RR_NATIVE_REPLAY
        NativeReplay(device.Get(), queue.Get(), allocator.Get(), commands.Get(), info.Get(), textures,
                     input, output, frame);
#endif
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
