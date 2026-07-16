#include "stdafx.h"

#include "StreamlineAdapter.h"

namespace Engine
{

namespace
{

TemporalUpscalerSupportInfo MakeUnavailableSupportInfo(TemporalUpscalerSupportStatus status)
{
    TemporalUpscalerSupportInfo info;
    info.backend = TemporalUpscalerBackend::Streamline;
    info.available = false;
    info.status = status;
    return info;
}

StreamlineEvaluateResult MakeUnavailableEvaluateResult(TemporalUpscalerSupportStatus status)
{
    StreamlineEvaluateResult result;
    result.outputAvailable = false;
    result.status = status;
    return result;
}

#if defined(RTPBRSURVEY_HAS_STREAMLINE_SDK)
// Streamline SDK includes and calls stay in this translation unit.

TemporalUpscalerSupportInfo QueryStreamlineSupportWithSdk()
{
    return MakeUnavailableSupportInfo(TemporalUpscalerSupportStatus::InitializationFailed);
}

StreamlineEvaluateResult EvaluateStreamlineWithSdk(const StreamlineEvaluateInputs& inputs)
{
    UNREFERENCED_PARAMETER(inputs);
    return MakeUnavailableEvaluateResult(TemporalUpscalerSupportStatus::InitializationFailed);
}
#else
TemporalUpscalerSupportInfo QueryStreamlineSupportWithoutSdk()
{
    return MakeUnavailableSupportInfo(TemporalUpscalerSupportStatus::NotIntegrated);
}

StreamlineEvaluateResult EvaluateStreamlineWithoutSdk(const StreamlineEvaluateInputs& inputs)
{
    UNREFERENCED_PARAMETER(inputs);
    return MakeUnavailableEvaluateResult(TemporalUpscalerSupportStatus::NotIntegrated);
}
#endif

} // namespace

TemporalUpscalerSupportInfo QueryStreamlineSupport()
{
#if defined(RTPBRSURVEY_HAS_STREAMLINE_SDK)
    return QueryStreamlineSupportWithSdk();
#else
    return QueryStreamlineSupportWithoutSdk();
#endif
}

StreamlineEvaluateResult EvaluateStreamline(const StreamlineEvaluateInputs& inputs)
{
#if defined(RTPBRSURVEY_HAS_STREAMLINE_SDK)
    return EvaluateStreamlineWithSdk(inputs);
#else
    return EvaluateStreamlineWithoutSdk(inputs);
#endif
}

} // namespace Engine
