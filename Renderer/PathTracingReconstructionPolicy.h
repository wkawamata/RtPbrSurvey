#pragma once

#include <string_view>
#include <array>
#include <cstdint>

namespace Engine
{
enum class PathTracingReconstructionBackend
{
    Legacy,
    Native,
    Nrd,
    Rr,
};

const char* PathTracingReconstructionBackendName(PathTracingReconstructionBackend backend);
PathTracingReconstructionBackend EffectivePathTracingBackend(PathTracingReconstructionBackend backend, bool legacyNrd);

struct PathTracingReconstructionPlan
{
    PathTracingReconstructionBackend backend = PathTracingReconstructionBackend::Native;
    bool runNrd = false;
    bool runRrConversion = false;
    bool diagnosticWork = false;
};

PathTracingReconstructionPlan PlanPathTracingReconstruction(
    PathTracingReconstructionBackend backend, bool legacyNrd, bool diagnostics, bool inspectNrd, bool inspectRr);

enum class PathTracingReconstructionStatus
{
    Native,
    NotBuilt,
    BackendUnavailable,
    IncompatibleInputs,
    AwaitingEvaluation,
    Ready,
};

struct PathTracingReconstructionPresentation
{
    PathTracingReconstructionBackend requested = PathTracingReconstructionBackend::Native;
    PathTracingReconstructionBackend output = PathTracingReconstructionBackend::Native;
    PathTracingReconstructionStatus status = PathTracingReconstructionStatus::Native;
};

// Adapter supplies readiness after its own format/pixel/history checks.
// outputValid means safe for this consumer, not simply "SDK call returned".
PathTracingReconstructionPresentation ResolvePathTracingReconstructionPresentation(
    PathTracingReconstructionBackend requested, bool built, bool backendReady, bool inputsCompatible, bool outputValid);
const char* PathTracingReconstructionStatusName(PathTracingReconstructionStatus status);

// Shared reset requests/epochs, independent of SDK histories or GPU ownership.
// Acknowledge only after the adapter accepted the reset for its next history.
// This does not mark any output presentable or complete a GPU submission.
class PathTracingReconstructionHistoryState
{
public:
    void Invalidate(PathTracingReconstructionBackend preserved = PathTracingReconstructionBackend::Native);
    void RequestReset(PathTracingReconstructionBackend backend);
    void AcknowledgeReset(PathTracingReconstructionBackend backend);
    bool ResetRequired(PathTracingReconstructionBackend backend) const;
    uint64_t Epoch(PathTracingReconstructionBackend backend) const;

private:
    std::array<bool, 2> m_reset = {true, true};
    std::array<uint64_t, 2> m_epoch = {1, 1};
};

struct PathTracingStageTiming
{
    bool available = false;
    float milliseconds = 0;
};

struct PathTracingReconstructionTimings
{
    PathTracingStageTiming nrdConversion;
    PathTracingStageTiming nrdReconstruction;
    PathTracingStageTiming nrdSdk;
    PathTracingStageTiming nrdComposition;
    PathTracingStageTiming rrConversion;
    PathTracingStageTiming rrSdk;
};

struct PathTracingReconstructionStageDescription
{
    const char* key;
    const char* label;
    PathTracingStageTiming PathTracingReconstructionTimings::* timing;
};

// One registry for UI labels and machine-readable stage keys.
inline constexpr std::array<PathTracingReconstructionStageDescription, 6> kPathTracingReconstructionStages = {{
    {"nrdConversion", "NRD input conversion", &PathTracingReconstructionTimings::nrdConversion},
    {"nrdReconstruction", "NRD reconstruction/admission", &PathTracingReconstructionTimings::nrdReconstruction},
    {"nrdSdk", "NRD SDK", &PathTracingReconstructionTimings::nrdSdk},
    {"nrdComposition", "NRD composition", &PathTracingReconstructionTimings::nrdComposition},
    {"rrConversion", "RR input conversion", &PathTracingReconstructionTimings::rrConversion},
    {"rrSdk", "RR SDK", &PathTracingReconstructionTimings::rrSdk},
}};

// Existing GPU pass timestamps, including pass transitions. No new readback/wait.
void AddPathTracingReconstructionTiming(PathTracingReconstructionTimings& timings,
                                        std::string_view pass,
                                        float milliseconds);
} // namespace Engine
