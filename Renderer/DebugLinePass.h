#pragma once

#include "Runtime/DebugLine.h"

#include <d3d12.h>
#include <d3dx12_core.h>
#include <wrl/client.h>
#include <DirectXMath.h>
#include <vector>

namespace Engine
{

using DebugLineVertex = RtPbrSurvey::DebugLineVertex;
static constexpr UINT kMaxDebugLines = RtPbrSurvey::kMaxHostDebugLines + 3;
static constexpr UINT kMaxDebugVertices = kMaxDebugLines * 2;

class DebugLinePass
{
public:
    DebugLinePass() = default;
    ~DebugLinePass() = default;

    DebugLinePass(const DebugLinePass&) = delete;
    DebugLinePass& operator=(const DebugLinePass&) = delete;

    void Create(
        ID3D12Device* device,
        D3D12_SHADER_BYTECODE vs,
        D3D12_SHADER_BYTECODE ps);

    void UpdateLines(
        const std::vector<DebugLineVertex>& depthTested,
        const std::vector<DebugLineVertex>& overlay,
        ID3D12GraphicsCommandList* commandList);

    void RecordDraw(
        ID3D12GraphicsCommandList* commandList,
        D3D12_GPU_VIRTUAL_ADDRESS viewProjCbv) const;

    ID3D12RootSignature* RootSignature() const { return m_rootSignature.Get(); }

private:
    void CreateRootSignature(ID3D12Device* device);
    void CreatePipelineStates(ID3D12Device* device, D3D12_SHADER_BYTECODE vs, D3D12_SHADER_BYTECODE ps);
    void CreateVertexBuffer(ID3D12Device* device);

    Microsoft::WRL::ComPtr<ID3D12RootSignature> m_rootSignature;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_depthTestedPipelineState;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_overlayPipelineState;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_vertexBuffer;
    D3D12_VERTEX_BUFFER_VIEW m_vertexBufferView = {};
    UINT m_depthTestedVertexCount = 0;
    UINT m_overlayVertexCount = 0;
};

} // namespace Engine
