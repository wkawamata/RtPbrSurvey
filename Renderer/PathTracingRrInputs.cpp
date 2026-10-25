#include "PathTracingRrInputs.h"

namespace Engine
{

PathTracingRrInputValidation InspectPathTracingRrMetadata(const PathTracingReconstructionInputs& inputs,
                                                          bool actualContinuation)
{
    const PathTracingInputValidation common = ValidatePathTracingReconstructionInputs(inputs);
    if (!common.Passed())
        return {PathTracingRrInputIssue::CommonContract, common, common.signal};
    const auto required = actualContinuation ? std::span<const PathTracingInputSignal>(kPathTracingRrLiveSignals)
                                             : std::span<const PathTracingInputSignal>(kPathTracingRrSignals);
    const PathTracingInputSignal missing = FindMissingPathTracingSignal(inputs, required);
    if (missing != PathTracingInputSignal::Count)
    {
        return {PathTracingRrInputIssue::MissingConversionSignal, {}, missing};
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
    const PathTracingFrameStamp pixelStamp = {
        pixels.frameId, pixels.samplingSignature, pixels.sampleStartIndex, pixels.width, pixels.height};
    if (pixelStamp != MakePathTracingFrameStamp(frame))
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
