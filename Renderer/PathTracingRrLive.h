#pragma once

#include "PathTracingRrHistory.h"
#include "StreamlineAdapter.h"

#include <wrl/client.h>
#include <string>

namespace Engine
{
// Step 2: one immutable, static production. Continuous PT history is separate.
// All GPU work stays on the caller's queue. No CPU fence waits or image copies.
class PathTracingRrLive
{
public:
    void Initialize(ID3D12Device* device, D3D12_SHADER_BYTECODE validationShader);
    void Poll(uint64_t completedFence, uint64_t currentEpoch);
    void MarkSubmitted(uint64_t fence);
    bool CanCapture() const;
    bool HoldProduction() const;
    bool CanEvaluate() const;
    bool OutputReady() const;
    const char* Status() const;
    const std::string& Failure() const
    {
        return m_failure;
    }
    uint64_t FrameId() const
    {
        return m_key.frameId;
    }
    uint64_t Epoch() const
    {
        return m_epoch;
    }
    void RecordInput(ID3D12GraphicsCommandList* commands,
                     D3D12_GPU_DESCRIPTOR_HANDLE statusSrv,
                     const PathTracingRrHistoryKey& key,
                     const RayReconstructionFrameConstants& constants,
                     const std::array<ID3D12Resource*, 8>& inputs,
                     ID3D12Resource* output);
    void RecordEvaluate(ID3D12GraphicsCommandList* commands,
                        ID3D12DescriptorHeap* heap,
                        D3D12_GPU_DESCRIPTOR_HANDLE outputSrv);

private:
    enum class Phase
    {
        Idle,
        InputPending,
        Evaluate,
        OutputPending,
        Ready,
        Rejected
    };
    void RecordValidation(ID3D12GraphicsCommandList* commands, D3D12_GPU_DESCRIPTOR_HANDLE srv, bool output);
    Phase m_phase = Phase::Idle;
    uint64_t m_epoch = 0;
    uint64_t m_fence = 0;
    bool m_submissionPending = false;
    uint64_t m_ticket = 0;
    std::string m_failure;
    PathTracingRrHistory m_history;
    PathTracingRrHistoryKey m_key;
    RayReconstructionFrameConstants m_constants;
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, 8> m_inputs;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_output;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_summary;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_zero;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_readback;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> m_signature;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_pipeline;
};
} // namespace Engine
