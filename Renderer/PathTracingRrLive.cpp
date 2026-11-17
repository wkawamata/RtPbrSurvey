#include "stdafx.h"

#include "PathTracingRrLive.h"
#include "Shared/Error.h"

#include <cstring>
#include <stdexcept>
#include <limits>

namespace Engine
{
void PathTracingRrLive::Initialize(ID3D12Device* device, D3D12_SHADER_BYTECODE shader)
{
    CD3DX12_DESCRIPTOR_RANGE1 range;
    range.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
    CD3DX12_ROOT_PARAMETER1 parameters[3];
    parameters[0].InitAsDescriptorTable(1, &range);
    parameters[1].InitAsUnorderedAccessView(0);
    parameters[2].InitAsConstants(5, 0);
    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC desc;
    desc.Init_1_1(3, parameters);
    Microsoft::WRL::ComPtr<ID3DBlob> signature, error;
    ThrowIfFailed(D3DX12SerializeVersionedRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1_1, &signature, &error));
    ThrowIfFailed(device->CreateRootSignature(
        0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&m_signature)));
    D3D12_COMPUTE_PIPELINE_STATE_DESC pipeline = {};
    pipeline.pRootSignature = m_signature.Get();
    pipeline.CS = shader;
    ThrowIfFailed(device->CreateComputePipelineState(&pipeline, IID_PPV_ARGS(&m_pipeline)));
    const auto buffer = CD3DX12_RESOURCE_DESC::Buffer(16, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    const auto defaultHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    for (auto& summary : m_summaries)
        ThrowIfFailed(device->CreateCommittedResource(
            &defaultHeap, D3D12_HEAP_FLAG_NONE, &buffer, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
            IID_PPV_ARGS(&summary)));
    const auto plain = CD3DX12_RESOURCE_DESC::Buffer(32);
    const auto uploadHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
    ThrowIfFailed(device->CreateCommittedResource(
        &uploadHeap, D3D12_HEAP_FLAG_NONE, &plain, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_zero)));
    void* mapped;
    const D3D12_RANGE noRead = {};
    ThrowIfFailed(m_zero->Map(0, &noRead, &mapped));
    memset(mapped, 0, 32);
    m_zero->Unmap(0, nullptr);
    const auto readbackHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_READBACK);
    ThrowIfFailed(device->CreateCommittedResource(&readbackHeap,
                                                  D3D12_HEAP_FLAG_NONE,
                                                  &plain,
                                                  D3D12_RESOURCE_STATE_COPY_DEST,
                                                  nullptr,
                                                  IID_PPV_ARGS(&m_readback)));
}

bool PathTracingRrLive::CanCapture() const
{
    return m_phase == Phase::Idle;
}
bool PathTracingRrLive::HoldProduction() const
{
    return m_phase != Phase::Idle && m_phase != Phase::Rejected;
}
bool PathTracingRrLive::CanEvaluate() const
{
    return m_phase == Phase::Evaluate;
}
bool PathTracingRrLive::OutputReady() const
{
    return m_publishedReady && m_publishedOutput != nullptr && m_publishedKey.resetToken == m_epoch;
}
const char* PathTracingRrLive::Status() const
{
    switch (m_phase)
    {
        case Phase::Idle:
            return "waiting-production";
        case Phase::InputPending:
            return "input-gpu-pending";
        case Phase::Evaluate:
            return "input-admitted";
        case Phase::OutputPending:
            return "output-gpu-pending";
        case Phase::Ready:
            return "retained-output-ready";
        case Phase::DrainingRejected:
            return "rejected-gpu-pending";
        case Phase::Rejected:
            return "native-fallback";
    }
    return "unknown";
}

void PathTracingRrLive::MarkSubmitted(uint64_t fence)
{
    if (m_submissionPending)
    {
        m_fence = fence;
        m_submissionPending = false;
    }
}

void PathTracingRrLive::Poll(uint64_t completedFence, uint64_t currentEpoch, bool advanceProduction, bool eagerProduction)
{
    if (m_phase == Phase::InputPending)
        ++m_packetLatency.inputPolls;
    else if (m_phase == Phase::OutputPending)
        ++m_packetLatency.outputPolls;
    if (completedFence == (std::numeric_limits<uint64_t>::max)())
        throw std::runtime_error("RR device removed before packet completion");
    // Drain cancelled work before reusing its summary/readback allocations.
    if (m_submissionPending || (m_fence != 0 && completedFence < m_fence))
        return;
    if (currentEpoch != m_epoch)
    {
        if (m_phase == Phase::InputPending || m_phase == Phase::Evaluate || m_phase == Phase::OutputPending ||
            m_phase == Phase::DrainingRejected)
            ++m_cancelledCount;
        m_history.Invalidate();
        m_historyDirty = true;
        m_phase = Phase::Idle;
        m_epoch = currentEpoch;
        m_failure.clear();
        m_fence = 0;
        m_output.Reset();
        m_publishedOutput.Reset();
        m_publishedReady = false;
        m_presented = false;
        for (auto& input : m_inputs)
            input.Reset();
        return;
    }
    if (m_phase == Phase::DrainingRejected)
    {
        m_fence = 0;
        m_history.Invalidate();
        m_historyDirty = true;
        m_phase = Phase::Rejected;
    }
    if (m_phase == Phase::Rejected && m_retryInput && advanceProduction)
    {
        // A transient unsupported sample must not permanently disable live RR.
        // Keep whole-frame Native fallback; only a newly admitted packet may
        // re-enter the SDK, with history reset after the rejected production.
        m_history.Invalidate();
        m_historyDirty = true;
        m_phase = Phase::Idle;
        m_failure.clear();
        m_retryInput = false;
    }
    if (m_phase == Phase::Ready && (m_presented || eagerProduction) && advanceProduction)
    {
        m_phase = Phase::Idle;
        m_presented = false;
    }
    if (m_fence == 0 || (m_phase != Phase::InputPending && m_phase != Phase::OutputPending))
        return;
    const D3D12_RANGE read = {0, 32};
    void* mapped;
    ThrowIfFailed(m_readback->Map(0, &read, &mapped));
    uint32_t counts[8];
    memcpy(counts, mapped, sizeof(counts));
    const D3D12_RANGE noWrite = {};
    m_readback->Unmap(0, &noWrite);
    m_fence = 0;
    const auto passed = [&](unsigned int offset)
    {
        const uint64_t serial = uint64_t(counts[offset + 2]) | (uint64_t(counts[offset + 3]) << 32);
        return serial == m_productionSerial && counts[offset] == 0 &&
            counts[offset + 1] == uint64_t(m_key.renderWidth) * m_key.renderHeight;
    };
    const bool inputPassed = passed(0);
    const bool outputPassed = m_phase != Phase::OutputPending || passed(4);
    if (!inputPassed || !outputPassed)
    {
        m_retryInput = m_chainedPacket || m_phase == Phase::InputPending;
        if (!inputPassed)
            ++m_inputRejectionCount;
        m_failure = !inputPassed ? "unsupported/stale RR input pixels" : "incomplete/nonfinite/stale RR output";
        m_history.Invalidate();
        m_historyDirty = true;
        if (!m_chainedPacket)
            m_publishedReady = false;
        m_phase = Phase::Rejected;
        return;
    }
    if (m_phase == Phase::InputPending)
    {
        m_inputAdmitted = Clock::now();
        m_packetLatency.inputWaitMs = std::chrono::duration<double, std::milli>(
            m_inputAdmitted - m_packetStarted).count();
        m_phase = Phase::Evaluate;
    }
    else
    {
        const auto completion = m_chainedPacket ? m_history.FinishSpeculative(m_ticket, true, true, true)
                                                : m_history.Finish(m_ticket, true, true);
        if (completion == PathTracingRrCompletion::Completed &&
            m_history.CanPresent(m_key, m_output.Get()))
        {
            m_publishedKey = m_key;
            m_publishedOutput = m_output;
            m_publishedSlot = m_outputSlot;
            m_publishedReady = true;
            m_presented = false;
            const auto now = Clock::now();
            m_packetLatency.outputWaitMs = std::chrono::duration<double, std::milli>(now - m_outputRecorded).count();
            m_packetLatency.totalMs = std::chrono::duration<double, std::milli>(now - m_packetStarted).count();
            m_lastPacketLatency = m_packetLatency;
            m_historyDirty = false;
            ++m_completedCount;
            // A verified published image is immutable and the next production
            // uses the other output slot. No presentation-only tick is needed.
            m_phase = eagerProduction && advanceProduction ? Phase::Idle : Phase::Ready;
        }
        else
        {
            m_history.Invalidate();
            m_historyDirty = true;
            m_publishedReady = false;
            m_phase = Phase::Rejected;
        }
    }
}

void PathTracingRrLive::RecordValidation(ID3D12GraphicsCommandList* commands,
                                         D3D12_GPU_DESCRIPTOR_HANDLE srv,
                                         bool output)
{
    ID3D12Resource* summary = m_summaries[output ? 1 : 0].Get();
    commands->CopyBufferRegion(summary, 0, m_zero.Get(), 0, 16);
    auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        summary, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    commands->ResourceBarrier(1, &barrier);
    commands->SetComputeRootSignature(m_signature.Get());
    commands->SetPipelineState(m_pipeline.Get());
    commands->SetComputeRootDescriptorTable(0, srv);
    commands->SetComputeRootUnorderedAccessView(1, summary->GetGPUVirtualAddress());
    const UINT constants[] = {m_key.renderWidth, m_key.renderHeight, output ? 1u : 0u,
        static_cast<UINT>(m_productionSerial), static_cast<UINT>(m_productionSerial >> 32)};
    commands->SetComputeRoot32BitConstants(2, 5, constants, 0);
    commands->Dispatch((m_key.renderWidth + 7) / 8, (m_key.renderHeight + 7) / 8, 1);
    barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        summary, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    commands->ResourceBarrier(1, &barrier);
    commands->CopyBufferRegion(m_readback.Get(), output ? 16 : 0, summary, 0, 16);
    barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        summary, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
    commands->ResourceBarrier(1, &barrier);
    m_submissionPending = true;
}

void PathTracingRrLive::RecordInput(ID3D12GraphicsCommandList* commands,
                                    D3D12_GPU_DESCRIPTOR_HANDLE statusSrv,
                                    const PathTracingRrHistoryKey& key,
                                    const RayReconstructionFrameConstants& constants,
                                    const std::array<ID3D12Resource*, 8>& inputs,
                                    ID3D12Resource* output,
                                    unsigned int outputSlot, bool chained)
{
    if (!CanCapture())
        throw std::logic_error("RR production already retained");
    if (m_productionSerial == (std::numeric_limits<uint64_t>::max)())
        throw std::overflow_error("RR production serial exhausted");
    ++m_productionSerial;
    m_chainedPacket = chained;
    m_packetStarted = Clock::now();
    m_packetLatency = {};
    m_packetLatency.frameId = key.frameId;
    m_key = key;
    m_retryInput = false;
    m_constants = constants;
    for (size_t i = 0; i < inputs.size(); ++i)
        m_inputs[i] = inputs[i];
    m_output = output;
    m_outputSlot = outputSlot;
    if (m_publishedReady && m_output.Get() == m_publishedOutput.Get())
        throw std::logic_error("RR cannot overwrite its published output");
    RecordValidation(commands, statusSrv, false);
    m_phase = chained ? Phase::Evaluate : Phase::InputPending;
}

void PathTracingRrLive::RecordEvaluate(ID3D12GraphicsCommandList* commands,
                                       ID3D12DescriptorHeap* heap,
                                       D3D12_GPU_DESCRIPTOR_HANDLE outputSrv)
{
    if (!CanEvaluate())
        throw std::logic_error("RR evaluation without a retained input packet");
    const auto decision = m_chainedPacket ? m_history.BeginSpeculative(m_key, m_output.Get())
                                          : m_history.Begin(m_key, m_output.Get(), true, true);
    if (decision.action != PathTracingRrHistoryAction::Evaluate)
        throw std::logic_error("RR retained history rejected");
    m_ticket = decision.ticket;
    m_lastReset = decision.reset;
    if (decision.reset)
        ++m_resetCount;
    else
        ++m_continuousCount;
    RayReconstructionEvaluateInputs inputs;
    inputs.pathTracingDiagnostic = true;
    inputs.commandList = commands;
    inputs.scalingInputColor = m_inputs[0].Get();
    inputs.albedo = m_inputs[1].Get();
    inputs.specularAlbedo = m_inputs[2].Get();
    inputs.normal = m_inputs[3].Get();
    inputs.roughness = m_inputs[4].Get();
    inputs.depth = m_inputs[5].Get();
    inputs.motionVectors = m_inputs[6].Get();
    inputs.specularHitDistance = m_inputs[7].Get();
    inputs.scalingOutputColor = m_output.Get();
    inputs.renderWidth = m_key.renderWidth;
    inputs.renderHeight = m_key.renderHeight;
    inputs.historyReset = decision.reset;
    inputs.historyValid = !decision.reset;
    inputs.enableNativeEvaluation = true;
    inputs.frameConstants = m_constants;
    const auto evaluateStarted = Clock::now();
    m_packetLatency.admittedWaitMs = m_chainedPacket ? 0 : std::chrono::duration<double, std::milli>(
        evaluateStarted - m_inputAdmitted).count();
    const auto result = EvaluateStreamlineRayReconstruction(inputs);
    m_packetLatency.evaluateCpuMs = std::chrono::duration<double, std::milli>(
        Clock::now() - evaluateStarted).count();
    m_history.Finish(m_ticket, result.outputAvailable, false);
    commands->SetDescriptorHeaps(1, &heap);
    if (!result.outputAvailable)
    {
        m_failure = result.inputReady ? QueryStreamlineRayReconstructionDiagnostics().lastEvaluateResultName
                                      : ToString(result.inputReadinessReason);
        m_historyDirty = true;
        if (!m_chainedPacket)
            m_publishedReady = false;
        m_retryInput = m_chainedPacket;
        // The SDK may have recorded commands before reporting failure.
        m_submissionPending = true;
        m_phase = Phase::DrainingRejected;
        return;
    }
    auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        m_output.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    commands->ResourceBarrier(1, &barrier);
    RecordValidation(commands, outputSrv, true);
    barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        m_output.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    commands->ResourceBarrier(1, &barrier);
    m_outputRecorded = Clock::now();
    m_phase = Phase::OutputPending;
}
} // namespace Engine
