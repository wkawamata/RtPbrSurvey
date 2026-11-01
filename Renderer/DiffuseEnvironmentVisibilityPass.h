#pragma once

#include <d3d12.h>
#include <wrl/client.h>

namespace Engine
{
struct DiffuseEnvironmentVisibilityConstants
{
    float normalBias = 0.001f;
    float rayTMin = 0.0001f;
    float traceDistance = 10000.0f;
    UINT sampleCount = 16;
    UINT supported = 1;
    UINT sampleRotationEnabled = 1;
};
static_assert(sizeof(DiffuseEnvironmentVisibilityConstants) == 6 * sizeof(UINT));

struct DiffuseEnvironmentVisibilityPassDesc
{
    D3D12_GPU_DESCRIPTOR_HANDLE maskUav = {};
    D3D12_GPU_DESCRIPTOR_HANDLE tlasSrv = {};
    D3D12_GPU_DESCRIPTOR_HANDLE depthSrv = {};
    D3D12_GPU_DESCRIPTOR_HANDLE normalSrv = {};
    D3D12_GPU_DESCRIPTOR_HANDLE cameraCbv = {};
    DiffuseEnvironmentVisibilityConstants constants;
    UINT width = 0;
    UINT height = 0;
};

class DiffuseEnvironmentVisibilityPass
{
public:
    void Create(ID3D12Device* device, D3D12_SHADER_BYTECODE shader);
    void Record(ID3D12GraphicsCommandList* commandList, const DiffuseEnvironmentVisibilityPassDesc& desc) const;

private:
    Microsoft::WRL::ComPtr<ID3D12RootSignature> m_rootSignature;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_pipelineState;
};
} // namespace Engine
