#include "stdafx.h"

#include "StreamlineAdapter.h"

#if defined(RTPBRSURVEY_HAS_STREAMLINE_SDK)
#include <sl.h>
#include <sl_dlss.h>
#endif

namespace Engine
{

namespace
{

struct StreamlineAdapterState
{
    TemporalUpscalerSupportStatus status =
#if defined(RTPBRSURVEY_HAS_STREAMLINE_SDK)
        TemporalUpscalerSupportStatus::InitializationFailed;
#else
        TemporalUpscalerSupportStatus::NotIntegrated;
#endif
    bool initialized = false;
};

StreamlineAdapterState g_streamlineAdapterState;

TemporalUpscalerSupportInfo MakeSupportInfo(TemporalUpscalerSupportStatus status)
{
    TemporalUpscalerSupportInfo info;
    info.backend = TemporalUpscalerBackend::Streamline;
    info.available = status == TemporalUpscalerSupportStatus::Available;
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
TemporalUpscalerSupportStatus InitializeStreamlineAdapterWithSdk(const StreamlineAdapterInitDesc& desc)
{
    UNREFERENCED_PARAMETER(desc);
    return TemporalUpscalerSupportStatus::InitializationFailed;
}

void ShutdownStreamlineAdapterWithSdk()
{
}

TemporalUpscalerSupportInfo QueryStreamlineSupportWithSdk()
{
    return MakeSupportInfo(g_streamlineAdapterState.status);
}

StreamlineEvaluateResult EvaluateStreamlineWithSdk(const StreamlineEvaluateInputs& inputs)
{
    UNREFERENCED_PARAMETER(inputs);
    return MakeUnavailableEvaluateResult(g_streamlineAdapterState.status);
}
#else
TemporalUpscalerSupportStatus InitializeStreamlineAdapterWithoutSdk(const StreamlineAdapterInitDesc& desc)
{
    UNREFERENCED_PARAMETER(desc);
    return TemporalUpscalerSupportStatus::NotIntegrated;
}

void ShutdownStreamlineAdapterWithoutSdk()
{
}

TemporalUpscalerSupportInfo QueryStreamlineSupportWithoutSdk()
{
    return MakeSupportInfo(g_streamlineAdapterState.status);
}

StreamlineEvaluateResult EvaluateStreamlineWithoutSdk(const StreamlineEvaluateInputs& inputs)
{
    UNREFERENCED_PARAMETER(inputs);
    return MakeUnavailableEvaluateResult(g_streamlineAdapterState.status);
}
#endif

} // namespace

TemporalUpscalerSupportInfo InitializeStreamlineAdapter(const StreamlineAdapterInitDesc& desc)
{
#if defined(RTPBRSURVEY_HAS_STREAMLINE_SDK)
    g_streamlineAdapterState.status = InitializeStreamlineAdapterWithSdk(desc);
#else
    g_streamlineAdapterState.status = InitializeStreamlineAdapterWithoutSdk(desc);
#endif
    g_streamlineAdapterState.initialized = g_streamlineAdapterState.status == TemporalUpscalerSupportStatus::Available;
    return MakeSupportInfo(g_streamlineAdapterState.status);
}

void ShutdownStreamlineAdapter()
{
#if defined(RTPBRSURVEY_HAS_STREAMLINE_SDK)
    ShutdownStreamlineAdapterWithSdk();
#else
    ShutdownStreamlineAdapterWithoutSdk();
#endif
    g_streamlineAdapterState = {};
}

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
