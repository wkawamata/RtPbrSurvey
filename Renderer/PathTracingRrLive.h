#pragma once

#include "PathTracingRrHistory.h"
#include "StreamlineAdapter.h"

#include <wrl/client.h>
#include <string>

namespace Engine
{
// One immutable production at a time; a verified output stays published while
// the next production is admitted/evaluated into the other slot.
// All GPU work stays on the caller's queue. No CPU fence waits or image copies.
class PathTracingRrLive
{
public:
    void Initialize(ID3D12Device* device, D3D12_SHADER_BYTECODE validationShader);
    void Poll(uint64_t completedFence, uint64_t currentEpoch, bool advanceProduction = false);
    void MarkSubmitted(uint64_t fence);
    void MarkPresented()
    {
        m_presented = true;
    }
    bool CanCapture() const;
    bool HoldProduction() const;
    bool CanEvaluate() const;
    bool OutputReady() const;
    const PathTracingRrHistoryKey* PublishedKey() const
    {
        return OutputReady() ? &m_publishedKey : nullptr;
    }
    const PathTracingRrHistoryKey& InputKey() const
    {
        return m_key;
    }
    const RayReconstructionFrameConstants& InputConstants() const
    {
        return m_constants;
    }
    const char* Status() const;
    const std::string& Failure() const
    {
        return m_failure;
    }
    uint64_t FrameId() const
    {
        return m_publishedReady ? m_publishedKey.frameId : m_key.frameId;
    }
    uint64_t InputFrameId() const
    {
        return m_key.frameId;
    }
    uint64_t CompletedCount() const
    {
        return m_completedCount;
    }
    uint64_t ContinuousCount() const
    {
        return m_continuousCount;
    }
    uint64_t ResetCount() const
    {
        return m_resetCount;
    }
    uint64_t CancelledCount() const
    {
        return m_cancelledCount;
    }
    uint64_t InputRejectionCount() const
    {
        return m_inputRejectionCount;
    }
    bool LastReset() const
    {
        return m_lastReset;
    }
    unsigned int PublishedSlot() const
    {
        return m_publishedSlot;
    }
    unsigned int NextOutputSlot() const
    {
        return m_publishedReady ? m_publishedSlot ^ 1u : 0u;
    }
    unsigned int WritingSlot() const
    {
        return m_outputSlot;
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
                     ID3D12Resource* output,
                     unsigned int outputSlot = 0);
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
    bool m_presented = false;
    bool m_publishedReady = false;
    bool m_lastReset = true;
    bool m_retryInput = false;
    unsigned int m_outputSlot = 0;
    unsigned int m_publishedSlot = 0;
    uint64_t m_completedCount = 0;
    uint64_t m_continuousCount = 0;
    uint64_t m_resetCount = 0;
    uint64_t m_cancelledCount = 0;
    uint64_t m_inputRejectionCount = 0;
    std::string m_failure;
    PathTracingRrHistory m_history;
    PathTracingRrHistoryKey m_key;
    PathTracingRrHistoryKey m_publishedKey;
    RayReconstructionFrameConstants m_constants;
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, 8> m_inputs;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_output;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_publishedOutput;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_summary;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_zero;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_readback;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> m_signature;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_pipeline;
};
} // namespace Engine
