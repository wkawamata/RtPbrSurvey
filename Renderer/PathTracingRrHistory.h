#pragma once

#include <array>
#include <cstdint>

namespace Engine
{

// Supplied only for an admitted, immutable production. Pixel/backend admission
// and ownership of GPU allocations/fences remain the caller's responsibility.
// Sample stride belongs to the immutable batch. Reset tokens track discontinuities.
struct PathTracingRrHistoryKey
{
    uint64_t frameId = 0;
    uint64_t samplingSignature = 0;
    uint64_t resetToken = 0;
    uint32_t sampleStartIndex = 0;
    uint32_t samplesPerFrame = 1;
    std::array<float, 2> jitterPixels = {};
    uint32_t renderWidth = 0;
    uint32_t renderHeight = 0;
    uint32_t outputWidth = 0;
    uint32_t outputHeight = 0;
    std::array<float, 16> viewProjection = {};
    std::array<float, 16> previousViewProjection = {};
    bool operator==(const PathTracingRrHistoryKey&) const = default;
};

enum class PathTracingRrHistoryAction
{
    NativeFallback,
    Evaluate,
    ReuseOutput,
};

enum class PathTracingRrHistoryReason
{
    Disabled,
    InputsRejected,
    InvalidKey,
    PendingEvaluation,
    StaleProduction,
    FirstEvaluation,
    Continuous,
    ResetTokenChanged,
    DimensionsChanged,
    SamplingChanged,
    FrameGap,
    SampleGap,
    CameraHistoryMismatch,
    CompletedProduction,
};

struct PathTracingRrHistoryDecision
{
    PathTracingRrHistoryAction action = PathTracingRrHistoryAction::NativeFallback;
    PathTracingRrHistoryReason reason = PathTracingRrHistoryReason::InvalidKey;
    bool reset = true;
    uint64_t ticket = 0;
};

enum class PathTracingRrCompletion
{
    StaleTicket,
    PendingGpu,
    Failed,
    Completed,
};

class PathTracingRrHistory
{
public:
    PathTracingRrHistoryDecision
    Begin(const PathTracingRrHistoryKey& key, const void* outputAllocation, bool requested, bool inputsAdmitted);
    // false verification means "not ready". A failed fence/pixel check must
    // invalidate the state. Retain allocations while pending or presentable.
    PathTracingRrCompletion Finish(uint64_t ticket, bool sdkOutputAvailable, bool gpuOutputVerified);
    bool CanPresent(const PathTracingRrHistoryKey& expected, const void* outputAllocation) const;
    // Does not cancel queued GPU work or recycle ticket/production identities.
    void Invalidate();

private:
    bool m_hasHistory = false;
    bool m_outputReady = false;
    uint64_t m_lastSeenFrame = 0;
    uint64_t m_lastTicket = 0;
    uint64_t m_pendingTicket = 0;
    PathTracingRrHistoryKey m_lastSuccess;
    PathTracingRrHistoryKey m_pending;
    const void* m_outputAllocation = nullptr;
    const void* m_pendingAllocation = nullptr;
};

} // namespace Engine
