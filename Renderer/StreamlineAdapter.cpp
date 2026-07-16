#include "stdafx.h"

#include "StreamlineAdapter.h"

namespace Engine
{

#if defined(RTPBRSURVEY_HAS_STREAMLINE_SDK)
// Streamline SDK includes and calls stay in this translation unit.
#endif

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
