#include "PathTracingRrInputs.h"

namespace Engine
{

PathTracingRrInputValidation InspectPathTracingRrMetadata(const PathTracingReconstructionInputs& inputs)
{
    const PathTracingInputValidation common = ValidatePathTracingReconstructionInputs(inputs);
    if (!common.Passed())
        return {PathTracingRrInputIssue::CommonContract, common, common.signal};
    for (PathTracingInputSignal signal : kPathTracingRrSignals)
    {
        if (!inputs.signals[static_cast<size_t>(signal)].present)
            return {PathTracingRrInputIssue::MissingConversionSignal, {}, signal};
    }
    return {};
}

PathTracingRrInputValidation InspectPathTracingRrFrame(const PathTracingReconstructionInputs& inputs,
                                                       const PathTracingRrPixelSummary& pixels)
{
    const PathTracingRrInputValidation metadata = InspectPathTracingRrMetadata(inputs);
    if (!metadata.Passed())
        return metadata;
    if (!pixels.completed)
        return {PathTracingRrInputIssue::PendingPixels};
    const PathTracingReconstructionFrame& frame = inputs.frame;
    if (pixels.frameId != frame.frameId || pixels.samplingSignature != frame.samplingSignature ||
        pixels.sampleStartIndex != frame.sampleStartIndex || pixels.width != frame.renderWidth ||
        pixels.height != frame.renderHeight)
        return {PathTracingRrInputIssue::StalePixels};
    uint64_t remaining = uint64_t(frame.renderWidth) * frame.renderHeight;
    for (uint64_t count : pixels.counts)
    {
        // Subtraction avoids accepting a wrapped uint64 sum.
        if (count > remaining)
            return {PathTracingRrInputIssue::IncompleteCoverage};
        remaining -= count;
    }
    if (remaining != 0)
        return {PathTracingRrInputIssue::IncompleteCoverage};
    for (size_t status = 1; status < pixels.counts.size(); ++status)
    {
        if (pixels.counts[status] != 0)
            return {PathTracingRrInputIssue::UnsupportedPixels};
    }
    return {};
}

} // namespace Engine
