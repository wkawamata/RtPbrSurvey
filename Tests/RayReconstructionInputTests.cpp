#include "Renderer/StreamlineAdapter.h"

#include <dxgi1_4.h>
#include <wrl/client.h>
#include <iostream>
#include <stdexcept>

namespace
{
using Microsoft::WRL::ComPtr;
void Require(HRESULT value)
{
    if (FAILED(value))
        throw std::runtime_error("D3D12 setup failed");
}
ComPtr<ID3D12Resource> Texture(ID3D12Device* device, DXGI_FORMAT format)
{
    D3D12_HEAP_PROPERTIES heap = {};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = desc.Height = 16;
    desc.DepthOrArraySize = desc.MipLevels = desc.SampleDesc.Count = 1;
    desc.Format = format;
    ComPtr<ID3D12Resource> result;
    Require(device->CreateCommittedResource(
        &heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&result)));
    return result;
}
void Check(bool condition, const char* description)
{
    if (!condition)
        throw std::runtime_error(description);
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
        ComPtr<ID3D12CommandAllocator> allocator;
        Require(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)));
        ComPtr<ID3D12GraphicsCommandList> commands;
        Require(device->CreateCommandList(
            0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&commands)));
        auto halfColor = Texture(device.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT);
        auto fullColor = Texture(device.Get(), DXGI_FORMAT_R32G32B32A32_FLOAT);
        auto scalar = Texture(device.Get(), DXGI_FORMAT_R32_FLOAT);
        auto motion = Texture(device.Get(), DXGI_FORMAT_R32G32_FLOAT);
        Engine::RayReconstructionEvaluateInputs inputs;
        inputs.commandList = commands.Get();
        inputs.renderWidth = inputs.renderHeight = 16;
        inputs.reflectionEvaluatedRadiance = halfColor.Get();
        inputs.reflectionResolvedRadiance = halfColor.Get();
        inputs.scalingInputColor = halfColor.Get();
        inputs.normal = halfColor.Get();
        inputs.albedo = inputs.specularAlbedo = halfColor.Get();
        inputs.depth = inputs.roughness = inputs.specularHitDistance = scalar.Get();
        inputs.motionVectors = motion.Get();
        const std::array<float, 16> identity = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
        auto& constants = inputs.frameConstants;
        constants.temporal.cameraViewToClip = constants.temporal.clipToCameraView = identity;
        constants.temporal.clipToPrevClip = constants.temporal.prevClipToClip = identity;
        constants.worldToCameraView = constants.cameraViewToWorld = identity;
        // Deliberately no SDK initialization: test readiness without native evaluation.
        Check(Engine::EvaluateStreamlineRayReconstruction(inputs).inputReady, "Hybrid resource contract changed");
        inputs.reflectionEvaluatedRadiance = nullptr;
        Check(!Engine::EvaluateStreamlineRayReconstruction(inputs).inputReady, "Hybrid missing reflection admitted");
        inputs.pathTracingDiagnostic = true;
        inputs.reflectionResolvedRadiance = nullptr;
        inputs.scalingInputColor = inputs.scalingOutputColor = fullColor.Get();
        inputs.albedo = inputs.specularAlbedo = inputs.normal = fullColor.Get();
        Check(Engine::EvaluateStreamlineRayReconstruction(inputs).inputReady, "PT resources rejected");
        inputs.scalingOutputColor = nullptr;
        Check(!Engine::EvaluateStreamlineRayReconstruction(inputs).inputReady, "PT missing output admitted");
        inputs.scalingOutputColor = fullColor.Get();
        inputs.pathTracingDiagnostic = false;
        Check(!Engine::EvaluateStreamlineRayReconstruction(inputs).inputReady,
              "PT resources leaked into Hybrid contract");
        Check(Engine::QueryStreamlineRayReconstructionDiagnostics().nativeEvaluateCallCount == 0,
              "Readiness checks must not invoke native RR");
        Require(commands->Close());
        std::cout << "Hybrid/PT input boundaries passed; no SDK evaluation requested\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
