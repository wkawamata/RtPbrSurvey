#pragma once

#include <d3d12.h>
#include <DirectXMath.h>

namespace Engine
{

struct HybridReflectionPassDesc
{
    ID3D12RootSignature* rootSignature = nullptr;
    ID3D12PipelineState* pipelineState = nullptr;
    D3D12_GPU_DESCRIPTOR_HANDLE reflectionRayHitUav = {};
    D3D12_GPU_DESCRIPTOR_HANDLE reflectionRayColorUav = {};
    D3D12_GPU_DESCRIPTOR_HANDLE tlasSrv = {};
    D3D12_GPU_DESCRIPTOR_HANDLE depthSrv = {};
    D3D12_GPU_DESCRIPTOR_HANDLE normalSrv = {};
    D3D12_GPU_DESCRIPTOR_HANDLE pbrParamsSrv = {};
    D3D12_GPU_DESCRIPTOR_HANDLE cameraCbv = {};
    D3D12_GPU_DESCRIPTOR_HANDLE materialBufferSrv = {};
    D3D12_GPU_DESCRIPTOR_HANDLE textureTableSrv = {};
    float normalBias = 0.01f;
    float rayTMin = 0.001f;
    float rayTMax = 10000.0f;
    float maxRoughness = 1.0f;
    float minMetallic = 0.0f;
    D3D12_GPU_VIRTUAL_ADDRESS vertexBufferSrv = 0;
    D3D12_GPU_VIRTUAL_ADDRESS indexBufferSrv = 0;
    D3D12_GPU_VIRTUAL_ADDRESS instanceBufferSrv = 0;
    UINT usesIndexedDraw = 0;
    UINT vertexCount = 0;
    UINT indexCount = 0;
    UINT hitNormalSource = 0;
    DirectX::XMFLOAT3 lightDirection = {0.0f, 1.0f, -1.0f};
    UINT directLightEnabled = 1;
    DirectX::XMFLOAT3 lightColor = {1.0f, 1.0f, 1.0f};
    float diffuseIntensity = 1.0f;
    UINT width = 0;
    UINT height = 0;
};

void RecordHybridReflectionPass(ID3D12GraphicsCommandList* commandList, const HybridReflectionPassDesc& desc);

} // namespace Engine
