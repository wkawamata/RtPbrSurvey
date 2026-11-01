#include "stdafx.h"

#include "DiffuseEnvironmentVisibilityPass.h"

#include "Shared/Error.h"
#include <d3dx12_root_signature.h>

namespace Engine
{
void DiffuseEnvironmentVisibilityPass::Create(ID3D12Device* device, D3D12_SHADER_BYTECODE shader)
{
    CD3DX12_DESCRIPTOR_RANGE1 ranges[5] = {};
    ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);
    ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
    ranges[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1);
    ranges[3].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 2);
    ranges[4].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0);
    CD3DX12_ROOT_PARAMETER1 parameters[6] = {};
    for (UINT i = 0; i < 5; ++i)
    {
        parameters[i].InitAsDescriptorTable(1, &ranges[i]);
    }
    parameters[5].InitAsConstants(6, 1);
    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC desc;
    desc.Init_1_1(6, parameters, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE);
    D3D12_FEATURE_DATA_ROOT_SIGNATURE feature = {D3D_ROOT_SIGNATURE_VERSION_1_1};
    if (FAILED(device->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &feature, sizeof(feature))))
    {
        feature.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
    }
    Microsoft::WRL::ComPtr<ID3DBlob> signature;
    Microsoft::WRL::ComPtr<ID3DBlob> error;
    ThrowIfFailed(D3DX12SerializeVersionedRootSignature(&desc, feature.HighestVersion, &signature, &error));
    ThrowIfFailed(device->CreateRootSignature(
        0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature)));
    D3D12_COMPUTE_PIPELINE_STATE_DESC pipeline = {};
    pipeline.pRootSignature = m_rootSignature.Get();
    pipeline.CS = shader;
    ThrowIfFailed(device->CreateComputePipelineState(&pipeline, IID_PPV_ARGS(&m_pipelineState)));
}

void DiffuseEnvironmentVisibilityPass::Record(ID3D12GraphicsCommandList* commandList,
                                              const DiffuseEnvironmentVisibilityPassDesc& desc) const
{
    commandList->SetComputeRootSignature(m_rootSignature.Get());
    commandList->SetPipelineState(m_pipelineState.Get());
    commandList->SetComputeRootDescriptorTable(0, desc.maskUav);
    commandList->SetComputeRootDescriptorTable(1, desc.tlasSrv);
    commandList->SetComputeRootDescriptorTable(2, desc.depthSrv);
    commandList->SetComputeRootDescriptorTable(3, desc.normalSrv);
    commandList->SetComputeRootDescriptorTable(4, desc.cameraCbv);
    commandList->SetComputeRoot32BitConstants(5, 6, &desc.constants, 0);
    commandList->Dispatch((desc.width + 7) / 8, (desc.height + 7) / 8, 1);
}
} // namespace Engine
