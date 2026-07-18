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
    bool sdkInitialized = false;
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

StreamlineDlssOptimalSettingsResult MakeUnavailableOptimalSettingsResult(TemporalUpscalerSupportStatus status)
{
    StreamlineDlssOptimalSettingsResult result;
    result.available = false;
    result.status = status;
    return result;
}

#if defined(RTPBRSURVEY_HAS_STREAMLINE_SDK)
sl::DLSSMode ToStreamlineDlssMode(TemporalUpscalerQualityMode qualityMode)
{
    switch (qualityMode)
    {
        case TemporalUpscalerQualityMode::Native:
            return sl::DLSSMode::eDLAA;
        case TemporalUpscalerQualityMode::UltraQuality:
            return sl::DLSSMode::eUltraQuality;
        case TemporalUpscalerQualityMode::Quality:
            return sl::DLSSMode::eMaxQuality;
        case TemporalUpscalerQualityMode::Balanced:
            return sl::DLSSMode::eBalanced;
        case TemporalUpscalerQualityMode::Performance:
            return sl::DLSSMode::eMaxPerformance;
        case TemporalUpscalerQualityMode::UltraPerformance:
            return sl::DLSSMode::eUltraPerformance;
        default:
            return sl::DLSSMode::eOff;
    }
}

TemporalUpscalerSupportStatus ToSupportStatus(sl::Result result)
{
    switch (result)
    {
        case sl::Result::eOk:
            return TemporalUpscalerSupportStatus::Available;
        case sl::Result::eErrorAdapterNotSupported:
        case sl::Result::eErrorNoSupportedAdapterFound:
        case sl::Result::eErrorFeatureNotSupported:
            return TemporalUpscalerSupportStatus::UnsupportedAdapter;
        case sl::Result::eErrorNoPlugins:
        case sl::Result::eErrorFeatureMissing:
        case sl::Result::eErrorFeatureFailedToLoad:
            return TemporalUpscalerSupportStatus::MissingRuntime;
        default:
            return TemporalUpscalerSupportStatus::InitializationFailed;
    }
}

TemporalUpscalerSupportStatus InitializeStreamlineAdapterWithSdk(const StreamlineAdapterInitDesc& desc)
{
    UNREFERENCED_PARAMETER(desc);

    if (g_streamlineAdapterState.sdkInitialized)
    {
        return g_streamlineAdapterState.status;
    }

    const sl::Feature featuresToLoad[] = {sl::kFeatureDLSS};
    sl::Preferences preferences = {};
    preferences.featuresToLoad = featuresToLoad;
    preferences.numFeaturesToLoad = _countof(featuresToLoad);
    preferences.engine = sl::EngineType::eCustom;
    preferences.engineVersion = "1.0.0";
    preferences.renderAPI = sl::RenderAPI::eD3D12;

    const sl::Result initResult = slInit(preferences);
    if (initResult != sl::Result::eOk)
    {
        return ToSupportStatus(initResult);
    }

    g_streamlineAdapterState.sdkInitialized = true;
    return TemporalUpscalerSupportStatus::InitializationFailed;
}

TemporalUpscalerSupportStatus SetStreamlineD3DDeviceWithSdk(ID3D12Device* device)
{
    if (!g_streamlineAdapterState.sdkInitialized || device == nullptr)
    {
        return TemporalUpscalerSupportStatus::InitializationFailed;
    }

    const sl::Result setDeviceResult = slSetD3DDevice(device);
    if (setDeviceResult != sl::Result::eOk)
    {
        return ToSupportStatus(setDeviceResult);
    }

    LUID adapterLuid = device->GetAdapterLuid();
    sl::AdapterInfo adapterInfo = {};
    adapterInfo.deviceLUID = reinterpret_cast<std::uint8_t*>(&adapterLuid);
    adapterInfo.deviceLUIDSizeInBytes = sizeof(adapterLuid);
    return ToSupportStatus(slIsFeatureSupported(sl::kFeatureDLSS, adapterInfo));
}

void ShutdownStreamlineAdapterWithSdk()
{
    if (g_streamlineAdapterState.sdkInitialized)
    {
        slShutdown();
    }
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

StreamlineDlssOptimalSettingsResult QueryStreamlineDlssOptimalSettingsWithSdk(
    const StreamlineDlssOptimalSettingsInputs& inputs)
{
    if (!g_streamlineAdapterState.initialized ||
        g_streamlineAdapterState.status != TemporalUpscalerSupportStatus::Available)
    {
        return MakeUnavailableOptimalSettingsResult(g_streamlineAdapterState.status);
    }

    sl::DLSSOptions options = {};
    options.mode = ToStreamlineDlssMode(inputs.qualityMode);
    options.outputWidth = inputs.outputWidth;
    options.outputHeight = inputs.outputHeight;
    options.colorBuffersHDR = sl::Boolean::eTrue;
    options.useAutoExposure = sl::Boolean::eTrue;

    sl::DLSSOptimalSettings settings = {};
    const sl::Result queryResult = slDLSSGetOptimalSettings(options, settings);
    if (queryResult != sl::Result::eOk)
    {
        return MakeUnavailableOptimalSettingsResult(ToSupportStatus(queryResult));
    }

    StreamlineDlssOptimalSettingsResult result;
    result.available = true;
    result.status = TemporalUpscalerSupportStatus::Available;
    result.recommendedRenderWidth = settings.optimalRenderWidth;
    result.recommendedRenderHeight = settings.optimalRenderHeight;
    result.minRenderWidth = settings.renderWidthMin;
    result.minRenderHeight = settings.renderHeightMin;
    result.maxRenderWidth = settings.renderWidthMax;
    result.maxRenderHeight = settings.renderHeightMax;
    return result;
}
#else
TemporalUpscalerSupportStatus InitializeStreamlineAdapterWithoutSdk(const StreamlineAdapterInitDesc& desc)
{
    UNREFERENCED_PARAMETER(desc);
    return TemporalUpscalerSupportStatus::NotIntegrated;
}

TemporalUpscalerSupportStatus SetStreamlineD3DDeviceWithoutSdk(ID3D12Device* device)
{
    UNREFERENCED_PARAMETER(device);
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

StreamlineDlssOptimalSettingsResult QueryStreamlineDlssOptimalSettingsWithoutSdk(
    const StreamlineDlssOptimalSettingsInputs& inputs)
{
    UNREFERENCED_PARAMETER(inputs);
    return MakeUnavailableOptimalSettingsResult(g_streamlineAdapterState.status);
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

TemporalUpscalerSupportInfo SetStreamlineD3DDevice(ID3D12Device* device)
{
#if defined(RTPBRSURVEY_HAS_STREAMLINE_SDK)
    g_streamlineAdapterState.status = SetStreamlineD3DDeviceWithSdk(device);
#else
    g_streamlineAdapterState.status = SetStreamlineD3DDeviceWithoutSdk(device);
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

StreamlineDlssOptimalSettingsResult QueryStreamlineDlssOptimalSettings(
    const StreamlineDlssOptimalSettingsInputs& inputs)
{
#if defined(RTPBRSURVEY_HAS_STREAMLINE_SDK)
    return QueryStreamlineDlssOptimalSettingsWithSdk(inputs);
#else
    return QueryStreamlineDlssOptimalSettingsWithoutSdk(inputs);
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
