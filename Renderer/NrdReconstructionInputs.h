#pragma once

#include "PathTracingReconstructionInputs.h"

namespace Engine
{

// Register order in shaders_NrdConversion.hlsl (t0..t10). These are common
// producer signals, before material demodulation, packing or reconstruction.
inline constexpr std::array<PathTracingInputSignal, 11> kNrdRelaxConversionSignals = {
    PathTracingInputSignal::NormalRoughness,
    PathTracingInputSignal::BaseColor,
    PathTracingInputSignal::MaterialF0Metallic,
    PathTracingInputSignal::PhysicalDiffuse,
    PathTracingInputSignal::PhysicalSpecular,
    PathTracingInputSignal::DiffuseDistance,
    PathTracingInputSignal::SpecularDistance,
    PathTracingInputSignal::PrimaryPosition,
    PathTracingInputSignal::DiffuseDirection,
    PathTracingInputSignal::SpecularDirection,
    PathTracingInputSignal::ViewZ,
};

enum class NrdInputMetadataIssue
{
    None,
    CommonContract,
    MissingConversionSignal,
    MissingCompositionSignal,
};

struct NrdInputMetadataValidation
{
    NrdInputMetadataIssue issue = NrdInputMetadataIssue::None;
    PathTracingInputValidation common;
    PathTracingInputSignal signal = PathTracingInputSignal::Count;
    bool MetadataReady() const
    {
        return issue == NrdInputMetadataIssue::None;
    }
};

// This is not pixel admission or SDK support/readiness. Ray-state/range,
// material-factor and packing checks remain in the existing NRD conversion.
NrdInputMetadataValidation InspectNrdReconstructionInputs(const PathTracingReconstructionInputs& inputs,
                                                          bool requireComposition);

} // namespace Engine
