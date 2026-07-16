#include "stdafx.h"

#include "StreamlineAdapter.h"

namespace Engine
{

TemporalUpscalerSupportInfo QueryStreamlineSupport()
{
    TemporalUpscalerSupportInfo info;
    info.backend = TemporalUpscalerBackend::Streamline;
    info.available = false;
    info.status = TemporalUpscalerSupportStatus::NotIntegrated;
    return info;
}

StreamlineEvaluateResult EvaluateStreamline(const StreamlineEvaluateInputs& inputs)
{
    UNREFERENCED_PARAMETER(inputs);

    StreamlineEvaluateResult result;
    result.outputAvailable = false;
    result.status = TemporalUpscalerSupportStatus::NotIntegrated;
    return result;
}

} // namespace Engine
