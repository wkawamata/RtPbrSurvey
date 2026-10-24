#include "NrdReconstructionInputs.h"

#include <initializer_list>

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
    for (PathTracingInputSignal signal : kNrdRelaxConversionSignals)
    {
        if (!inputs.signals[static_cast<size_t>(signal)].present)
        {
            return {NrdInputMetadataIssue::MissingConversionSignal, {}, signal};
        }
    }
    if (requireComposition)
    {
        for (PathTracingInputSignal signal :
             {PathTracingInputSignal::StableRadiance, PathTracingInputSignal::BackgroundRadiance})
        {
            if (!inputs.signals[static_cast<size_t>(signal)].present)
            {
                return {NrdInputMetadataIssue::MissingCompositionSignal, {}, signal};
            }
        }
    }
    return {};
}

} // namespace Engine
