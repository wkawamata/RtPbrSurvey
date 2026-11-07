#include <windows.h>
#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;
namespace
{
void Check(HRESULT result)
{
    if (FAILED(result))
        throw std::runtime_error("D3D12 failure: " + std::to_string(result));
}
std::vector<char> Read(const char* path)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
        throw std::runtime_error("Shader not found");
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}
ComPtr<ID3D12Resource>
Buffer(ID3D12Device* device, UINT64 bytes, D3D12_HEAP_TYPE type, D3D12_RESOURCE_STATES state, bool uav = false)
{
    D3D12_HEAP_PROPERTIES heap = {};
    heap.Type = type;
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = bytes;
    desc.Height = desc.DepthOrArraySize = desc.MipLevels = desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    desc.Flags = uav ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS : D3D12_RESOURCE_FLAG_NONE;
    ComPtr<ID3D12Resource> result;
    Check(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr, IID_PPV_ARGS(&result)));
    return result;
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
} // namespace
int main(int argc, char** argv)
{
    try
    {
        if (argc != 3 && argc != 4)
            throw std::runtime_error("Expected optimized shader, reference shader, optional --hardware");
        const bool hardware = argc == 4 && std::string(argv[3]) == "--hardware";
        if (argc == 4 && !hardware)
            throw std::runtime_error("Unknown option");
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
        D3D12_DESCRIPTOR_RANGE range = {};
        range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        range.NumDescriptors = 1;
        D3D12_ROOT_PARAMETER parameters[3] = {};
        parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        parameters[0].DescriptorTable = {1, &range};
        parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
        parameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        parameters[2].Constants.Num32BitValues = 3;
        D3D12_ROOT_SIGNATURE_DESC rootDesc = {};
        rootDesc.NumParameters = 3;
        rootDesc.pParameters = parameters;
        ComPtr<ID3DBlob> blob, errors;
        Check(D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &errors));
        ComPtr<ID3D12RootSignature> root;
        Check(device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&root)));
        std::array<ComPtr<ID3D12PipelineState>, 2> pipelines;
        for (UINT index = 0; index < 2; ++index)
        {
            const auto shader = Read(argv[index + 1]);
            D3D12_COMPUTE_PIPELINE_STATE_DESC desc = {};
            desc.pRootSignature = root.Get();
            desc.CS = {shader.data(), shader.size()};
            Check(device->CreateComputePipelineState(&desc, IID_PPV_ARGS(&pipelines[index])));
        }
        D3D12_COMMAND_QUEUE_DESC queueDesc = {};
        ComPtr<ID3D12CommandQueue> queue;
        Check(device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&queue)));
        UINT64 frequency;
        Check(queue->GetTimestampFrequency(&frequency));
        ComPtr<ID3D12Fence> fence;
        Check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
        UINT64 signal = 0;
        D3D12_QUERY_HEAP_DESC queryDesc = {};
        queryDesc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
        queryDesc.Count = 2;
        ComPtr<ID3D12QueryHeap> queries;
        Check(device->CreateQueryHeap(&queryDesc, IID_PPV_ARGS(&queries)));
        D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
        heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        heapDesc.NumDescriptors = 1;
        heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        ComPtr<ID3D12DescriptorHeap> descriptors;
        Check(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&descriptors)));
        const std::array<std::array<UINT, 2>, 7> sizes = {
            {{1, 1}, {7, 9}, {8, 8}, {17, 19}, {65, 3}, {1280, 720}, {1920, 1080}}};
        UINT cases = 0;
        for (const auto& size : sizes)
            for (UINT mode = 0; mode < 2; ++mode)
                for (UINT pattern = 0; pattern < 4; ++pattern)
                {
                    const UINT width = size[0], height = size[1];
                    // Large dispatches cover valid and mixed-invalid inputs; small ones also
                    // cover every-invalid and RGB-valid/nonfinite-alpha cases.
                    if (width > 65 && pattern > 1)
                        continue;
                    D3D12_HEAP_PROPERTIES defaultHeap = {};
                    defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;
                    D3D12_RESOURCE_DESC desc = {};
                    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
                    desc.Width = width;
                    desc.Height = height;
                    desc.DepthOrArraySize = desc.MipLevels = desc.SampleDesc.Count = 1;
                    desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
                    ComPtr<ID3D12Resource> texture;
                    Check(device->CreateCommittedResource(&defaultHeap,
                                                          D3D12_HEAP_FLAG_NONE,
                                                          &desc,
                                                          D3D12_RESOURCE_STATE_COPY_DEST,
                                                          nullptr,
                                                          IID_PPV_ARGS(&texture)));
                    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint;
                    UINT64 uploadBytes;
                    device->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, nullptr, nullptr, &uploadBytes);
                    auto upload =
                        Buffer(device.Get(), uploadBytes, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
                    void* mapped;
                    const D3D12_RANGE noRead = {};
                    Check(upload->Map(0, &noRead, &mapped));
                    UINT expectedMask = 0;
                    const float nan = std::numeric_limits<float>::quiet_NaN();
                    const float inf = std::numeric_limits<float>::infinity();
                    const std::array<float, 14> statuses = {0, 1, 2, 3, 4, 5, 6, 1.5f, -1, 7, nan, inf, -inf, .5f};
                    for (UINT y = 0; y < height; ++y)
                        for (UINT x = 0; x < width; ++x)
                        {
                            const UINT index = y * width + x;
                            std::array<float, 4> value = {0, .5f, 1, 1};
                            if (pattern == 1 || pattern == 2)
                            {
                                if (mode == 0)
                                    value[0] = pattern == 2 ? -1.f : statuses[index % statuses.size()];
                                else if (pattern == 2 || index % 5 == 0)
                                    value[index % 3] = index % 2 ? nan : inf;
                            }
                            if (pattern == 3)
                                value[3] = nan; // alpha is intentionally ignored in both modes
                            const bool valid = mode != 0 ? std::isfinite(value[0]) && std::isfinite(value[1]) &&
                                                               std::isfinite(value[2])
                                                         : value[0] == 0;
                            if (!valid)
                            {
                                const UINT category =
                                    std::isfinite(value[0]) && value[0] >= 1 && value[0] <= 6 ? UINT(value[0]) : 31;
                                expectedMask |= mode != 0 ? 1u : (1u << category);
                            }
                            std::memcpy(static_cast<char*>(mapped) + y * footprint.Footprint.RowPitch + x * 16,
                                        value.data(),
                                        16);
                        }
                    upload->Unmap(0, nullptr);
                    D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
                    srv.Format = desc.Format;
                    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                    srv.Texture2D.MipLevels = 1;
                    device->CreateShaderResourceView(
                        texture.Get(), &srv, descriptors->GetCPUDescriptorHandleForHeapStart());
                    auto zeros = Buffer(device.Get(), 8, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
                    Check(zeros->Map(0, &noRead, &mapped));
                    std::memset(mapped, 0, 8);
                    zeros->Unmap(0, nullptr);
                    std::array<double, 2> duration = {};
                    for (UINT variant = 0; variant < 2; ++variant)
                    {
                        auto summary =
                            Buffer(device.Get(), 8, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COPY_DEST, true);
                        auto readback =
                            Buffer(device.Get(), 24, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
                        ComPtr<ID3D12CommandAllocator> allocator;
                        Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)));
                        ComPtr<ID3D12GraphicsCommandList> commands;
                        Check(device->CreateCommandList(0,
                                                        D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                        allocator.Get(),
                                                        pipelines[variant].Get(),
                                                        IID_PPV_ARGS(&commands)));
                        if (variant == 0)
                        {
                            D3D12_TEXTURE_COPY_LOCATION from = {};
                            from.pResource = upload.Get();
                            from.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
                            from.PlacedFootprint = footprint;
                            D3D12_TEXTURE_COPY_LOCATION to = {};
                            to.pResource = texture.Get();
                            commands->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
                            Transition(commands.Get(),
                                       texture.Get(),
                                       D3D12_RESOURCE_STATE_COPY_DEST,
                                       D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                        }
                        commands->CopyBufferRegion(summary.Get(), 0, zeros.Get(), 0, 8);
                        Transition(commands.Get(),
                                   summary.Get(),
                                   D3D12_RESOURCE_STATE_COPY_DEST,
                                   D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                        ID3D12DescriptorHeap* heaps[] = {descriptors.Get()};
                        commands->SetDescriptorHeaps(1, heaps);
                        commands->SetComputeRootSignature(root.Get());
                        commands->SetComputeRootDescriptorTable(0, descriptors->GetGPUDescriptorHandleForHeapStart());
                        commands->SetComputeRootUnorderedAccessView(1, summary->GetGPUVirtualAddress());
                        const UINT constants[] = {width, height, mode};
                        commands->SetComputeRoot32BitConstants(2, 3, constants, 0);
                        commands->EndQuery(queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0);
                        commands->Dispatch((width + 7) / 8, (height + 7) / 8, 1);
                        Transition(commands.Get(),
                                   summary.Get(),
                                   D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                   D3D12_RESOURCE_STATE_COPY_SOURCE);
                        commands->EndQuery(queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 1);
                        commands->ResolveQueryData(queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0, 2, readback.Get(), 8);
                        commands->CopyBufferRegion(readback.Get(), 0, summary.Get(), 0, 8);
                        Check(commands->Close());
                        ID3D12CommandList* lists[] = {commands.Get()};
                        queue->ExecuteCommandLists(1, lists);
                        Check(queue->Signal(fence.Get(), ++signal));
                        HANDLE event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
                        if (!event)
                            throw std::runtime_error("Event creation failed");
                        const HRESULT result = fence->SetEventOnCompletion(signal, event);
                        const DWORD wait = SUCCEEDED(result) ? WaitForSingleObject(event, 30000) : WAIT_FAILED;
                        CloseHandle(event);
                        Check(result);
                        if (wait != WAIT_OBJECT_0)
                            throw std::runtime_error("GPU test timed out");
                        const D3D12_RANGE read = {0, 24};
                        Check(readback->Map(0, &read, &mapped));
                        std::array<UINT, 2> actual;
                        std::array<UINT64, 2> times;
                        std::memcpy(actual.data(), mapped, 8);
                        std::memcpy(times.data(), static_cast<char*>(mapped) + 8, 16);
                        const D3D12_RANGE noWrite = {};
                        readback->Unmap(0, &noWrite);
                        if (actual[0] != expectedMask || actual[1] != width * height)
                            throw std::runtime_error("Summary mismatch: " + std::to_string(width) + "x" +
                                                     std::to_string(height) + " mode=" + std::to_string(mode) +
                                                     " pattern=" + std::to_string(pattern) +
                                                     " variant=" + std::to_string(variant));
                        duration[variant] = double(times[1] - times[0]) * 1000.0 / double(frequency);
                    }
                    ++cases;
                    if (width > 65)
                        std::cout << width << "x" << height << " mode=" << mode << " pattern=" << pattern
                                  << " optimizedMs=" << duration[0] << " referenceMs=" << duration[1] << "\n";
                }
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
        std::cout << (hardware ? "Hardware" : "WARP") << ": " << cases
                  << " cases, optimized/reference/CPU summaries identical; D3D12 errors 0\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
