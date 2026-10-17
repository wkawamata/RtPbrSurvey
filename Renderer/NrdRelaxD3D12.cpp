#include "stdafx.h"

#include "NrdRelaxD3D12.h"

#include <fstream>
#include <map>
#include <stdexcept>
#include <include/d3dx12/d3dx12.h>

namespace Engine
{
namespace
{
using Microsoft::WRL::ComPtr;
void Check(HRESULT result)
{
    if (FAILED(result))
        throw std::runtime_error("NRD D3D12 operation failed: " + std::to_string(result));
}

struct Texture
{
    ComPtr<ID3D12Resource> resource;
    D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;
};

struct Pipeline
{
    ComPtr<ID3D12RootSignature> root;
    ComPtr<ID3D12PipelineState> state;
    uint32_t srvParameter = UINT32_MAX;
    uint32_t uavParameter = UINT32_MAX;
};

struct FrameStorage
{
    ComPtr<ID3D12DescriptorHeap> heap;
    ComPtr<ID3D12Resource> constants;
    uint8_t* mapped = nullptr;
};
} // namespace

struct NrdRelaxD3D12::Impl
{
    NrdRelaxSdkAdapter sdk;
    ComPtr<ID3D12Device> device;
    std::vector<Pipeline> pipelines;
    std::vector<Texture> permanent, transient;
    std::vector<FrameStorage> frames;
    uint32_t width = 0, height = 0;
    uint32_t descriptorStride = 0, descriptorsPerDispatch = 0, constantStride = 0;

    void AllocatePool(const std::vector<NrdSdkTexture>& descriptions, std::vector<Texture>& pool)
    {
        const auto heap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
        for (const auto& description : descriptions)
        {
            const uint32_t factor = description.downsampleFactor;
            const auto desc = CD3DX12_RESOURCE_DESC::Tex2D(static_cast<DXGI_FORMAT>(description.dxgiFormat),
                                                           (width + factor - 1) / factor,
                                                           (height + factor - 1) / factor,
                                                           1,
                                                           1,
                                                           1,
                                                           0,
                                                           D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
            Texture texture;
            Check(device->CreateCommittedResource(
                &heap, D3D12_HEAP_FLAG_NONE, &desc, texture.state, nullptr, IID_PPV_ARGS(&texture.resource)));
            texture.resource->SetName(L"NRD RELAX private pool");
            pool.push_back(std::move(texture));
        }
    }
};

NrdRelaxD3D12::NrdRelaxD3D12() = default;
NrdRelaxD3D12::~NrdRelaxD3D12() = default;

void NrdRelaxD3D12::Initialize(ID3D12Device* device, const std::filesystem::path& directory, uint32_t frameCount)
{
    auto impl = std::make_unique<Impl>();
    std::string error;
    if (device == nullptr || frameCount == 0 || !impl->sdk.Initialize(error))
        throw std::runtime_error("NRD GPU initialization: " + error);
    impl->device = device;
    const auto& layout = impl->sdk.Layout();
    impl->constantStride = (impl->sdk.Description().maximumConstantBytes + 255) & ~255u;
    impl->descriptorStride = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    std::ifstream manifest(directory / "nrd-relax-manifest.txt");
    for (size_t i = 0; i < layout.pipelines.size(); ++i)
    {
        const auto& desc = layout.pipelines[i];
        std::string identifier;
        if (!std::getline(manifest, identifier) || identifier != desc.identifier)
            throw std::runtime_error("NRD shader manifest does not match the runtime SDK.");
        std::ifstream shader(directory / ("nrd-relax-" + std::to_string(i) + ".cso"), std::ios::binary);
        const std::vector<char> bytecode((std::istreambuf_iterator<char>(shader)), {});
        if (bytecode.empty())
            throw std::runtime_error("NRD shader bytecode is missing: " + identifier);
        CD3DX12_ROOT_PARAMETER roots[3];
        roots[0].InitAsConstantBufferView(layout.constantRegister, layout.constantSpace);
        CD3DX12_DESCRIPTOR_RANGE ranges[2];
        uint32_t count = 1;
        Pipeline pipeline;
        if (desc.textures != 0)
        {
            ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, desc.textures, 0, layout.resourceSpace);
            pipeline.srvParameter = count;
            roots[count++].InitAsDescriptorTable(1, &ranges[0]);
        }
        if (desc.storageTextures != 0)
        {
            ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, desc.storageTextures, 0, layout.resourceSpace);
            pipeline.uavParameter = count;
            roots[count++].InitAsDescriptorTable(1, &ranges[1]);
        }
        CD3DX12_STATIC_SAMPLER_DESC samplers[2];
        for (UINT s = 0; s < 2; ++s)
        {
            samplers[s].Init(s,
                             s == 0 ? D3D12_FILTER_MIN_MAG_MIP_POINT : D3D12_FILTER_MIN_MAG_MIP_LINEAR,
                             D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
                             D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
                             D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
                             0,
                             1,
                             D3D12_COMPARISON_FUNC_ALWAYS,
                             D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK,
                             0,
                             D3D12_FLOAT32_MAX,
                             D3D12_SHADER_VISIBILITY_ALL,
                             layout.constantSpace);
        }
        const CD3DX12_ROOT_SIGNATURE_DESC rootDesc(count, roots, 2, samplers);
        ComPtr<ID3DBlob> signature, errors;
        Check(D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &errors));
        Check(device->CreateRootSignature(
            0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&pipeline.root)));
        D3D12_COMPUTE_PIPELINE_STATE_DESC pipelineDesc = {};
        pipelineDesc.pRootSignature = pipeline.root.Get();
        pipelineDesc.CS = {bytecode.data(), bytecode.size()};
        Check(device->CreateComputePipelineState(&pipelineDesc, IID_PPV_ARGS(&pipeline.state)));
        impl->pipelines.push_back(std::move(pipeline));
        impl->descriptorsPerDispatch = (std::max)(impl->descriptorsPerDispatch, desc.textures + desc.storageTextures);
    }
    std::string extra;
    if (std::getline(manifest, extra))
        throw std::runtime_error("NRD shader manifest has unexpected pipelines.");
    if (layout.maximumDispatches == 0 || impl->constantStride == 0 || impl->descriptorsPerDispatch == 0)
        throw std::runtime_error("NRD SDK returned empty allocation limits.");
    const auto upload = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
    for (uint32_t i = 0; i < frameCount; ++i)
    {
        FrameStorage frame;
        D3D12_DESCRIPTOR_HEAP_DESC heap = {};
        heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        heap.NumDescriptors = layout.maximumDispatches * impl->descriptorsPerDispatch;
        heap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        Check(device->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&frame.heap)));
        const auto desc = CD3DX12_RESOURCE_DESC::Buffer(uint64_t(layout.maximumDispatches) * impl->constantStride);
        Check(device->CreateCommittedResource(&upload,
                                              D3D12_HEAP_FLAG_NONE,
                                              &desc,
                                              D3D12_RESOURCE_STATE_GENERIC_READ,
                                              nullptr,
                                              IID_PPV_ARGS(&frame.constants)));
        const D3D12_RANGE noRead = {0, 0};
        Check(frame.constants->Map(0, &noRead, reinterpret_cast<void**>(&frame.mapped)));
        impl->frames.push_back(std::move(frame));
    }
    m_impl = std::move(impl);
}

void NrdRelaxD3D12::ReleaseResolutionResources()
{
    if (m_impl != nullptr)
    {
        m_impl->permanent.clear();
        m_impl->transient.clear();
        m_impl->width = m_impl->height = 0;
    }
}

uint32_t NrdRelaxD3D12::Record(ID3D12GraphicsCommandList* commands,
                               uint32_t frameSlot,
                               const NrdSdkFrame& settings,
                               const NrdD3D12Inputs& inputs)
{
    if (m_impl == nullptr || commands == nullptr || frameSlot >= m_impl->frames.size())
        throw std::runtime_error("NRD GPU execution is not initialized.");
    auto& impl = *m_impl;
    const auto& layout = impl.sdk.Layout();
    std::map<NrdSdkResourceKind, ID3D12Resource*> external = {
        {NrdSdkResourceKind::Motion, inputs.motion},
        {NrdSdkResourceKind::NormalRoughness, inputs.normalRoughness},
        {NrdSdkResourceKind::ViewZ, inputs.viewZ},
        {NrdSdkResourceKind::DiffuseInput, inputs.diffuse},
        {NrdSdkResourceKind::SpecularInput, inputs.specular},
        {NrdSdkResourceKind::DiffuseOutput, inputs.diffuseOutput},
        {NrdSdkResourceKind::SpecularOutput, inputs.specularOutput}};
    std::map<ID3D12Resource*, D3D12_RESOURCE_STATES> externalStates, initialStates;
    for (const auto& entry : external)
    {
        if (entry.second == nullptr || entry.second->GetDesc().Width != settings.width ||
            entry.second->GetDesc().Height != settings.height)
            throw std::runtime_error("NRD external resource is absent or has the wrong dimensions.");
        const bool output =
            entry.first == NrdSdkResourceKind::DiffuseOutput || entry.first == NrdSdkResourceKind::SpecularOutput;
        externalStates[entry.second] =
            output ? D3D12_RESOURCE_STATE_UNORDERED_ACCESS : D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    }
    initialStates = externalStates;
    std::vector<NrdSdkDispatch> dispatches;
    std::string error;
    if (!impl.sdk.PrepareFrame(settings, dispatches, error))
        throw std::runtime_error(error);
    if (dispatches.size() > layout.maximumDispatches)
        throw std::runtime_error("NRD dispatch count exceeds allocation limits.");
    if (impl.width == 0)
    {
        if (!settings.reset)
            throw std::runtime_error("New NRD pools require a history reset.");
        impl.width = settings.width;
        impl.height = settings.height;
        impl.AllocatePool(layout.permanent, impl.permanent);
        impl.AllocatePool(layout.transient, impl.transient);
    }
    if (impl.width != settings.width || impl.height != settings.height)
        throw std::runtime_error("NRD resize requires GPU completion and pool retirement.");
    auto& frame = impl.frames[frameSlot];
    ID3D12DescriptorHeap* heaps[] = {frame.heap.Get()};
    commands->SetDescriptorHeaps(1, heaps);
    auto transition = [&](ID3D12Resource* resource, D3D12_RESOURCE_STATES& before, D3D12_RESOURCE_STATES after)
    {
        if (before != after)
        {
            const auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(resource, before, after);
            commands->ResourceBarrier(1, &barrier);
            before = after;
        }
    };
    for (uint32_t index = 0; index < dispatches.size(); ++index)
    {
        const auto& dispatch = dispatches[index];
        const auto& desc = layout.pipelines.at(dispatch.pipelineIndex);
        const auto& pipeline = impl.pipelines.at(dispatch.pipelineIndex);
        if (dispatch.bindings.size() != desc.textures + desc.storageTextures ||
            dispatch.constants.size() > impl.constantStride)
            throw std::runtime_error("NRD dispatch does not match its pipeline layout.");
        const uint32_t first = index * impl.descriptorsPerDispatch;
        for (uint32_t b = 0; b < dispatch.bindings.size(); ++b)
        {
            const auto& binding = dispatch.bindings[b];
            if (binding.storage != (b >= desc.textures))
                throw std::runtime_error("NRD descriptor ranges are not in SRV/UAV order.");
            ID3D12Resource* resource = nullptr;
            D3D12_RESOURCE_STATES* state = nullptr;
            if (binding.kind == NrdSdkResourceKind::Permanent || binding.kind == NrdSdkResourceKind::Transient)
            {
                auto& texture = (binding.kind == NrdSdkResourceKind::Permanent ? impl.permanent : impl.transient)
                                    .at(binding.poolIndex);
                resource = texture.resource.Get();
                state = &texture.state;
            }
            else
            {
                resource = external.at(binding.kind);
                state = &externalStates.at(resource);
            }
            transition(resource,
                       *state,
                       binding.storage ? D3D12_RESOURCE_STATE_UNORDERED_ACCESS
                                       : D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            const CD3DX12_CPU_DESCRIPTOR_HANDLE handle(
                frame.heap->GetCPUDescriptorHandleForHeapStart(), first + b, impl.descriptorStride);
            if (binding.storage)
            {
                D3D12_UNORDERED_ACCESS_VIEW_DESC view = {};
                view.Format = resource->GetDesc().Format;
                view.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
                impl.device->CreateUnorderedAccessView(resource, nullptr, &view, handle);
            }
            else
            {
                D3D12_SHADER_RESOURCE_VIEW_DESC view = {};
                view.Format = resource->GetDesc().Format;
                view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                view.Texture2D.MipLevels = 1;
                impl.device->CreateShaderResourceView(resource, &view, handle);
            }
        }
        const uint64_t constantOffset = uint64_t(index) * impl.constantStride;
        memset(frame.mapped + constantOffset, 0, impl.constantStride);
        if (!dispatch.constants.empty())
            memcpy(frame.mapped + constantOffset, dispatch.constants.data(), dispatch.constants.size());
        commands->SetComputeRootSignature(pipeline.root.Get());
        commands->SetPipelineState(pipeline.state.Get());
        commands->SetComputeRootConstantBufferView(0, frame.constants->GetGPUVirtualAddress() + constantOffset);
        if (pipeline.srvParameter != UINT32_MAX)
            commands->SetComputeRootDescriptorTable(
                pipeline.srvParameter,
                CD3DX12_GPU_DESCRIPTOR_HANDLE(
                    frame.heap->GetGPUDescriptorHandleForHeapStart(), first, impl.descriptorStride));
        if (pipeline.uavParameter != UINT32_MAX)
            commands->SetComputeRootDescriptorTable(
                pipeline.uavParameter,
                CD3DX12_GPU_DESCRIPTOR_HANDLE(
                    frame.heap->GetGPUDescriptorHandleForHeapStart(), first + desc.textures, impl.descriptorStride));
        commands->Dispatch(dispatch.gridWidth, dispatch.gridHeight, 1);
        const auto barrier = CD3DX12_RESOURCE_BARRIER::UAV(nullptr);
        commands->ResourceBarrier(1, &barrier);
    }
    for (const auto& entry : initialStates)
        transition(entry.first, externalStates.at(entry.first), entry.second);
    return static_cast<uint32_t>(dispatches.size());
}
} // namespace Engine
