#pragma once

#include "PathTracingReconstructionInputs.h"

namespace Engine
{

// t0..t7 in shaders_PathTracingRrConversion.hlsl. Always raw producer data.
inline constexpr std::array<PathTracingInputSignal, 8> kPathTracingRrSignals = {
    PathTracingInputSignal::FrameRadiance,
    PathTracingInputSignal::NormalRoughness,
    PathTracingInputSignal::ViewZ,
    PathTracingInputSignal::MotionVectors,
    PathTracingInputSignal::BaseColor,
    PathTracingInputSignal::MaterialF0Metallic,
    PathTracingInputSignal::PrimaryPosition,
    PathTracingInputSignal::SpecularDistance,
};

inline constexpr size_t kPathTracingRrStatusCount = 7;

// Fill only after completion of conversion/readback for the stamped production.
// Partial coverage is not sufficient for whole-frame numeric admission.
struct PathTracingRrPixelSummary
{
    bool completed = false;
    uint64_t frameId = 0;
    uint64_t samplingSignature = 0;
    uint32_t sampleStartIndex = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    std::array<uint64_t, kPathTracingRrStatusCount> counts = {};
};

enum class PathTracingRrInputIssue
{
    None,
    CommonContract,
    MissingConversionSignal,
    PendingPixels,
    StalePixels,
    IncompleteCoverage,
    UnsupportedPixels,
};

struct PathTracingRrInputValidation
{
    PathTracingRrInputIssue issue = PathTracingRrInputIssue::None;
    PathTracingInputValidation common;
    PathTracingInputSignal signal = PathTracingInputSignal::Count;
    bool Passed() const
    {
        return issue == PathTracingRrInputIssue::None;
    }
};

PathTracingRrInputValidation InspectPathTracingRrMetadata(const PathTracingReconstructionInputs& inputs);

// Numeric gate only. Caller must first validate resource identity/provenance.
// Success does not qualify specular ray/radiance correlation, SDK support,
// output extent/format, history, or native RR evaluation.
PathTracingRrInputValidation InspectPathTracingRrFrame(const PathTracingReconstructionInputs& inputs,
                                                       const PathTracingRrPixelSummary& pixels);

} // namespace Engine
