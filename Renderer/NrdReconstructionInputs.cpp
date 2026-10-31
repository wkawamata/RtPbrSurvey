#include "NrdReconstructionInputs.h"

namespace Engine
{

NrdInputMetadataValidation InspectNrdReconstructionInputs(const PathTracingReconstructionInputs& inputs,
                                                          bool requireComposition)
{
    const PathTracingInputValidation common = ValidatePathTracingReconstructionInputs(inputs);
    if (!common.Passed())
    {
        return {NrdInputMetadataIssue::CommonContract, common, common.signal};
    }
    if (inputs.frame.samplesPerFrame != 1 || inputs.frame.coherentPrimaryBatch || inputs.frame.frameJitterPrimary)
    {
        return {NrdInputMetadataIssue::CommonContract, {PathTracingInputIssue::UnsupportedSampling}};
    }
    const PathTracingInputSignal missing = FindMissingPathTracingSignal(inputs, kNrdRelaxConversionSignals);
    if (missing != PathTracingInputSignal::Count)
    {
        return {NrdInputMetadataIssue::MissingConversionSignal, {}, missing};
    }
    if (requireComposition)
    {
        static constexpr PathTracingInputSignal compositionSignals[] = {PathTracingInputSignal::StableRadiance,
                                                                        PathTracingInputSignal::BackgroundRadiance};
        const PathTracingInputSignal missingComposition = FindMissingPathTracingSignal(inputs, compositionSignals);
        if (missingComposition != PathTracingInputSignal::Count)
        {
            return {NrdInputMetadataIssue::MissingCompositionSignal, {}, missingComposition};
        }
    }
    return {};
}

} // namespace Engine
