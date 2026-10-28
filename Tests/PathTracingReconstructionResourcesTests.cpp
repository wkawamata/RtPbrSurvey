#include "Renderer/PathTracingReconstructionResources.h"

#include <dxgi1_4.h>
#include <iostream>
#include <stdexcept>

namespace
{
using Microsoft::WRL::ComPtr;
using Signal = Engine::PathTracingInputSignal;
using Issue = Engine::PathTracingInputIssue;

void Require(HRESULT result)
{
    if (FAILED(result))
    {
        throw std::runtime_error("D3D12 WARP test resource creation failed");
    }
}

ComPtr<ID3D12Resource> MakeTexture(ID3D12Device* device, DXGI_FORMAT format, UINT width = 16, UINT16 arraySize = 1)
{
    D3D12_HEAP_PROPERTIES heap = {};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width;
    desc.Height = 16;
    desc.DepthOrArraySize = arraySize;
    desc.MipLevels = 1;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    ComPtr<ID3D12Resource> resource;
    Require(device->CreateCommittedResource(
        &heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&resource)));
    return resource;
}

bool Check(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "FAILED: " << message << '\n';
    }
    return condition;
}
} // namespace

int main()
{
    try
    {
        ComPtr<IDXGIFactory4> factory;
        Require(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));
        ComPtr<IDXGIAdapter> warp;
        Require(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp)));
        ComPtr<ID3D12Device> device;
        Require(D3D12CreateDevice(warp.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)));

        Engine::PathTracingReconstructionFrame frame;
        frame.frameId = 42;
        frame.renderWidth = frame.renderHeight = frame.outputWidth = frame.outputHeight = 16;
        frame.pixelCenterPrimary = true;
        frame.samplingSignature = 7;
        const std::array<float, 16> identity = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
        frame.worldToView = frame.viewToClip = frame.previousViewProjection = identity;
        std::array<ComPtr<ID3D12Resource>, Engine::kPathTracingInputSignalCount> owned;
        Engine::PathTracingInputResources resources = {};
        for (size_t i = 0; i <= static_cast<size_t>(Signal::MaterialF0Metallic); ++i)
        {
            const DXGI_FORMAT format = i == static_cast<size_t>(Signal::ViewZ) ? DXGI_FORMAT_R32_FLOAT
                                       : i == static_cast<size_t>(Signal::MotionVectors)
                                           ? DXGI_FORMAT_R16G16_FLOAT
                                           : DXGI_FORMAT_R16G16B16A16_FLOAT;
            owned[i] = MakeTexture(device.Get(), format);
            resources[i] = owned[i].Get();
        }
        bool passed = true;
        Engine::PathTracingReconstructionProduction production;
        passed &= Check(production.Validate(42, resources).issue == Issue::MissingProduction,
                        "allocated resources alone are not evidence of production");
        production.Capture(frame, resources);
        passed &= Check(production.Validate(42, resources).Passed(), "actual D3D12 resource descriptions accepted");
        passed &= Check(production.Inputs().signals[0].width == 16 &&
                            production.Inputs().signals[0].format == DXGI_FORMAT_R16G16B16A16_FLOAT,
                        "metadata comes from GetDesc");
        passed &= Check(production.Validate(43, resources).issue == Issue::FrameMismatch &&
                            production.Inputs().frame.frameId == 42,
                        "consumer cannot relabel stale production");
        auto current = resources;
        auto replacement = MakeTexture(device.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT);
        current[0] = replacement.Get();
        const auto replaced = production.Validate(42, current);
        passed &= Check(replaced.issue == Issue::ResourceIdentityMismatch && replaced.signal == Signal::FrameRadiance,
                        "same-format replacement does not inherit production");
        current = resources;
        current[0] = nullptr;
        passed &= Check(production.Validate(42, current).issue == Issue::ResourceIdentityMismatch,
                        "removed resource rejected");
        auto resized = MakeTexture(device.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT, 32);
        current[0] = resized.Get();
        production.Capture(frame, current);
        passed &= Check(production.Validate(42, current).issue == Issue::ResourceSizeMismatch,
                        "reallocation descriptor must match the produced frame");
        auto array = MakeTexture(device.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT, 16, 2);
        current[0] = array.Get();
        production.Capture(frame, current);
        passed &= Check(production.Validate(42, current).issue == Issue::InvalidResourceShape,
                        "texture array cannot masquerade as one frame texture");
        current[0] = nullptr;
        production.Capture(frame, current);
        passed &= Check(production.Validate(42, current).issue == Issue::MissingRequiredSignal,
                        "producer missing required resource fails closed");
        production.Capture(frame, resources);
        production.Invalidate();
        passed &= Check(production.Validate(42, resources).issue == Issue::MissingProduction,
                        "pause or skipped dispatch cannot reuse the last production stamp");
        if (passed)
        {
            std::cout << "PT reconstruction D3D12 production binding passed\n";
        }
        return passed ? 0 : 1;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
