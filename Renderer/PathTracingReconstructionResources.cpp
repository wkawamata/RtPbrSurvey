#include "PathTracingReconstructionResources.h"

#include <limits>

namespace Engine
{

void PathTracingReconstructionProduction::Invalidate()
{
    m_recorded = false;
    m_inputs = {};
    m_shapeValidation = {};
    for (auto& resource : m_resources)
    {
        resource.Reset();
    }
}

void PathTracingReconstructionProduction::Capture(const PathTracingReconstructionFrame& frame,
                                                  const PathTracingInputResources& produced)
{
    Invalidate();
    m_inputs.frame = frame;
    for (size_t i = 0; i < produced.size(); ++i)
    {
        m_resources[i] = produced[i];
        if (produced[i] == nullptr)
        {
            continue;
        }
        const D3D12_RESOURCE_DESC desc = produced[i]->GetDesc();
        if (desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D || desc.DepthOrArraySize != 1 || desc.MipLevels != 1 ||
            desc.SampleDesc.Count != 1 || desc.Width > std::numeric_limits<uint32_t>::max() ||
            (desc.Flags & D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE) != 0)
        {
            if (m_shapeValidation.Passed())
            {
                m_shapeValidation = {PathTracingInputIssue::InvalidResourceShape,
                                     static_cast<PathTracingInputSignal>(i)};
            }
        }
        m_inputs.signals[i] = {true,
                               desc.Format,
                               static_cast<uint32_t>(desc.Width),
                               desc.Height,
                               frame.frameId,
                               frame.sampleStartIndex,
                               frame.samplesPerFrame,
                               frame.samplingSignature};
    }
    m_recorded = true;
}

PathTracingInputValidation PathTracingReconstructionProduction::Validate(uint64_t expectedFrameId,
                                                                         const PathTracingInputResources& current) const
{
    if (!m_recorded)
    {
        return {PathTracingInputIssue::MissingProduction};
    }
    if (m_inputs.frame.frameId != expectedFrameId)
    {
        return {PathTracingInputIssue::FrameMismatch};
    }
    for (size_t i = 0; i < current.size(); ++i)
    {
        if (current[i] != m_resources[i].Get())
        {
            return {PathTracingInputIssue::ResourceIdentityMismatch, static_cast<PathTracingInputSignal>(i)};
        }
    }
    if (!m_shapeValidation.Passed())
    {
        return m_shapeValidation;
    }
    return ValidatePathTracingReconstructionInputs(m_inputs);
}

const PathTracingReconstructionInputs& PathTracingReconstructionProduction::Inputs() const
{
    return m_inputs;
}

} // namespace Engine
