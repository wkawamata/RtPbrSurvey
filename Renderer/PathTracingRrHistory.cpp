#include "PathTracingRrHistory.h"

#include "PathTracingReconstructionInputs.h"

#include <limits>
#include <cmath>

namespace Engine
{
void PathTracingRrHistory::Invalidate()
{
    m_hasHistory = false;
    m_outputReady = false;
    m_pendingTicket = 0;
    m_outputAllocation = m_pendingAllocation = nullptr;
}

PathTracingRrHistoryDecision PathTracingRrHistory::Begin(const PathTracingRrHistoryKey& key,
                                                         const void* outputAllocation,
                                                         bool requested,
                                                         bool inputsAdmitted)
{
    using Action = PathTracingRrHistoryAction;
    using Reason = PathTracingRrHistoryReason;
    if (!requested || !inputsAdmitted)
    {
        Invalidate();
        return {Action::NativeFallback, requested ? Reason::InputsRejected : Reason::Disabled};
    }
    // A caller must finish/invalidate its in-flight submission before replacing
    // the packet. A different consumer frame cannot present its output.
    if (m_pendingTicket != 0)
        return {Action::NativeFallback, Reason::PendingEvaluation};
    if (key.frameId == 0 || key.samplingSignature == 0 || key.resetToken == 0 || key.renderWidth == 0 ||
        key.renderHeight == 0 || key.outputWidth == 0 || key.outputHeight == 0 || key.samplesPerFrame == 0 ||
        key.samplesPerFrame > 16 ||
        uint64_t(key.sampleStartIndex) + key.samplesPerFrame > std::numeric_limits<uint32_t>::max() ||
        !std::isfinite(key.jitterPixels[0]) || !std::isfinite(key.jitterPixels[1]) ||
        std::abs(key.jitterPixels[0]) > .5f || std::abs(key.jitterPixels[1]) > .5f || outputAllocation == nullptr ||
        !IsPathTracingCameraMatrixUsable(key.viewProjection) ||
        !IsPathTracingCameraMatrixUsable(key.previousViewProjection))
    {
        Invalidate();
        return {Action::NativeFallback, Reason::InvalidKey};
    }
    if (CanPresent(key, outputAllocation))
        return {Action::ReuseOutput, Reason::CompletedProduction, false};
    m_outputReady = false;
    if (key.frameId <= m_lastSeenFrame || m_lastTicket == std::numeric_limits<uint64_t>::max())
    {
        m_hasHistory = false;
        return {Action::NativeFallback, Reason::StaleProduction};
    }
    Reason reason = Reason::FirstEvaluation;
    if (m_hasHistory)
    {
        const auto& previous = m_lastSuccess;
        if (key.resetToken != previous.resetToken)
            reason = Reason::ResetTokenChanged;
        else if (key.renderWidth != previous.renderWidth || key.renderHeight != previous.renderHeight ||
                 key.outputWidth != previous.outputWidth || key.outputHeight != previous.outputHeight)
            reason = Reason::DimensionsChanged;
        else if (key.samplingSignature != previous.samplingSignature || key.samplesPerFrame != previous.samplesPerFrame)
            reason = Reason::SamplingChanged;
        else if (key.frameId != previous.frameId + 1)
            reason = Reason::FrameGap;
        else if (uint64_t(key.sampleStartIndex) != uint64_t(previous.sampleStartIndex) + previous.samplesPerFrame)
            reason = Reason::SampleGap;
        else if (key.previousViewProjection != previous.viewProjection)
            reason = Reason::CameraHistoryMismatch;
        else
            reason = Reason::Continuous;
    }
    m_lastSeenFrame = key.frameId;
    m_pending = key;
    m_pendingAllocation = outputAllocation;
    m_pendingTicket = ++m_lastTicket;
    return {Action::Evaluate, reason, reason != Reason::Continuous, m_pendingTicket};
}

PathTracingRrCompletion PathTracingRrHistory::Finish(uint64_t ticket, bool sdkOutputAvailable, bool gpuOutputVerified)
{
    if (ticket == 0 || ticket != m_pendingTicket)
        return PathTracingRrCompletion::StaleTicket;
    if (!sdkOutputAvailable)
    {
        Invalidate();
        return PathTracingRrCompletion::Failed;
    }
    if (!gpuOutputVerified)
        return PathTracingRrCompletion::PendingGpu;
    m_lastSuccess = m_pending;
    m_outputAllocation = m_pendingAllocation;
    m_pendingTicket = 0;
    m_pendingAllocation = nullptr;
    m_hasHistory = m_outputReady = true;
    return PathTracingRrCompletion::Completed;
}

bool PathTracingRrHistory::CanPresent(const PathTracingRrHistoryKey& expected, const void* outputAllocation) const
{
    return m_outputReady && outputAllocation != nullptr && outputAllocation == m_outputAllocation &&
           expected == m_lastSuccess;
}
} // namespace Engine
