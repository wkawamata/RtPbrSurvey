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
        TemporalUpscalerSupportStatus::DeviceNotSet;
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

StreamlineEvaluateResult MakeAvailableEvaluateResult()
{
    StreamlineEvaluateResult result;
    result.outputAvailable = true;
    result.status = TemporalUpscalerSupportStatus::Available;
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

sl::float4x4 ToStreamlineMatrix(const std::array<float, 16>& values)
{
    sl::float4x4 matrix = {};
    for (std::uint32_t row = 0; row < 4; ++row)
    {
        const std::uint32_t offset = row * 4;
        matrix.setRow(row, {values[offset], values[offset + 1], values[offset + 2], values[offset + 3]});
    }
    return matrix;
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
        case sl::Result::eErrorDriverOutOfDate:
            return TemporalUpscalerSupportStatus::DriverOutOfDate;
        case sl::Result::eErrorOSOutOfDate:
            return TemporalUpscalerSupportStatus::OperatingSystemOutOfDate;
        case sl::Result::eErrorOSDisabledHWS:
            return TemporalUpscalerSupportStatus::HardwareSchedulingDisabled;
        case sl::Result::eErrorDeviceNotCreated:
        case sl::Result::eErrorNotInitialized:
        case sl::Result::eErrorInitNotCalled:
            return TemporalUpscalerSupportStatus::DeviceNotSet;
        case sl::Result::eErrorIO:
        case sl::Result::eErrorNoPlugins:
        case sl::Result::eErrorMissingProxy:
        case sl::Result::eErrorFeatureMissing:
        case sl::Result::eErrorFeatureFailedToLoad:
        case sl::Result::eErrorFeatureMissingDependency:
            return TemporalUpscalerSupportStatus::MissingRuntime;
        case sl::Result::eErrorMissingResourceState:
        case sl::Result::eErrorInvalidIntegration:
        case sl::Result::eErrorMissingInputParameter:
        case sl::Result::eErrorInvalidParameter:
        case sl::Result::eErrorMissingConstants:
        case sl::Result::eErrorDuplicatedConstants:
        case sl::Result::eErrorMissingOrInvalidAPI:
        case sl::Result::eErrorCommonConstantsMissing:
        case sl::Result::eErrorUnsupportedInterface:
        case sl::Result::eErrorFeatureMissingHooks:
        case sl::Result::eErrorFeatureWrongPriority:
        case sl::Result::eErrorFeatureManagerInvalidState:
        case sl::Result::eErrorInvalidState:
            return TemporalUpscalerSupportStatus::InvalidIntegration;
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
    preferences.flags |= sl::PreferenceFlags::eUseFrameBasedResourceTagging;
    preferences.engine = sl::EngineType::eCustom;
    preferences.engineVersion = "1.0.0";
    preferences.projectId = "a0f57b54-1daf-4934-90ae-c4035c19df04";
    preferences.renderAPI = sl::RenderAPI::eD3D12;
#if defined(_DEBUG)
    preferences.logLevel = sl::LogLevel::eDefault;
    preferences.pathToLogsAndData = nullptr;
#endif

    const sl::Result initResult = slInit(preferences);
    if (initResult != sl::Result::eOk)
    {
        return ToSupportStatus(initResult);
    }

    g_streamlineAdapterState.sdkInitialized = true;
    return TemporalUpscalerSupportStatus::DeviceNotSet;
}

TemporalUpscalerSupportStatus SetStreamlineD3DDeviceWithSdk(ID3D12Device* device)
{
    if (!g_streamlineAdapterState.sdkInitialized || device == nullptr)
    {
        return TemporalUpscalerSupportStatus::DeviceNotSet;
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
    if (!g_streamlineAdapterState.initialized ||
        g_streamlineAdapterState.status != TemporalUpscalerSupportStatus::Available)
    {
        return MakeUnavailableEvaluateResult(g_streamlineAdapterState.status);
    }

    if (inputs.commandList == nullptr || inputs.inputSceneColor == nullptr || inputs.depth == nullptr ||
        inputs.motionVectors == nullptr || inputs.outputSceneColor == nullptr || inputs.renderWidth == 0 ||
        inputs.renderHeight == 0 || inputs.outputWidth == 0 || inputs.outputHeight == 0)
    {
        return MakeUnavailableEvaluateResult(TemporalUpscalerSupportStatus::InvalidIntegration);
    }

    sl::FrameToken* frameToken = nullptr;
    const sl::Result frameTokenResult = slGetNewFrameToken(frameToken);
    if (frameTokenResult != sl::Result::eOk || frameToken == nullptr)
    {
        return MakeUnavailableEvaluateResult(ToSupportStatus(frameTokenResult));
    }

    const sl::ViewportHandle viewport = 0;
    sl::DLSSOptions options = {};
    options.mode = ToStreamlineDlssMode(inputs.settings.qualityMode);
    options.outputWidth = inputs.outputWidth;
    options.outputHeight = inputs.outputHeight;
    options.colorBuffersHDR = sl::Boolean::eTrue;
    options.useAutoExposure = inputs.settings.autoExposure ? sl::Boolean::eTrue : sl::Boolean::eFalse;
    const sl::Result optionsResult = slDLSSSetOptions(viewport, options);
    if (optionsResult != sl::Result::eOk)
    {
        return MakeUnavailableEvaluateResult(ToSupportStatus(optionsResult));
    }

    const TemporalUpscalerFrameConstants& source = inputs.frameConstants;
    sl::Constants constants = {};
    constants.cameraViewToClip = ToStreamlineMatrix(source.cameraViewToClip);
    constants.clipToCameraView = ToStreamlineMatrix(source.clipToCameraView);
    constants.clipToPrevClip = ToStreamlineMatrix(source.clipToPrevClip);
    constants.prevClipToClip = ToStreamlineMatrix(source.prevClipToClip);
    constants.jitterOffset = {source.jitterOffset[0], source.jitterOffset[1]};
    constants.mvecScale = {inputs.settings.motionVectorScale[0], inputs.settings.motionVectorScale[1]};
    constants.cameraPinholeOffset = {0.0f, 0.0f};
    constants.cameraPos = {source.cameraPosition[0], source.cameraPosition[1], source.cameraPosition[2]};
    constants.cameraUp = {source.cameraUp[0], source.cameraUp[1], source.cameraUp[2]};
    constants.cameraRight = {source.cameraRight[0], source.cameraRight[1], source.cameraRight[2]};
    constants.cameraFwd = {source.cameraForward[0], source.cameraForward[1], source.cameraForward[2]};
    constants.cameraNear = source.cameraNear;
    constants.cameraFar = source.cameraFar;
    constants.cameraFOV = source.cameraFovRadians;
    constants.cameraAspectRatio = source.cameraAspectRatio;
    constants.depthInverted = source.depthInverted ? sl::Boolean::eTrue : sl::Boolean::eFalse;
    constants.cameraMotionIncluded = source.cameraMotionIncluded ? sl::Boolean::eTrue : sl::Boolean::eFalse;
    constants.motionVectors3D = sl::Boolean::eFalse;
    constants.reset = inputs.historyReset ? sl::Boolean::eTrue : sl::Boolean::eFalse;
    constants.orthographicProjection = sl::Boolean::eFalse;
    constants.motionVectorsDilated = sl::Boolean::eFalse;
    constants.motionVectorsJittered = sl::Boolean::eFalse;

    const sl::Result constantsResult = slSetConstants(constants, *frameToken, viewport);
    if (constantsResult != sl::Result::eOk)
    {
        return MakeUnavailableEvaluateResult(ToSupportStatus(constantsResult));
    }

    sl::Extent renderExtent = {};
    renderExtent.width = inputs.renderWidth;
    renderExtent.height = inputs.renderHeight;
    sl::Extent outputExtent = {};
    outputExtent.width = inputs.outputWidth;
    outputExtent.height = inputs.outputHeight;

    sl::Resource inputColor(sl::ResourceType::eTex2d, inputs.inputSceneColor, D3D12_RESOURCE_STATE_COPY_SOURCE);
    sl::Resource depth(sl::ResourceType::eTex2d, inputs.depth, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    sl::Resource motionVectors(
        sl::ResourceType::eTex2d, inputs.motionVectors, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    sl::Resource outputColor(sl::ResourceType::eTex2d, inputs.outputSceneColor, D3D12_RESOURCE_STATE_COPY_DEST);
    const sl::ResourceTag tags[] = {
        {&inputColor, sl::kBufferTypeScalingInputColor, sl::ResourceLifecycle::eOnlyValidNow, &renderExtent},
        {&depth, sl::kBufferTypeDepth, sl::ResourceLifecycle::eOnlyValidNow, &renderExtent},
        {&motionVectors, sl::kBufferTypeMotionVectors, sl::ResourceLifecycle::eOnlyValidNow, &renderExtent},
        {&outputColor, sl::kBufferTypeScalingOutputColor, sl::ResourceLifecycle::eOnlyValidNow, &outputExtent},
    };
    const sl::Result tagResult = slSetTagForFrame(*frameToken, viewport, tags, _countof(tags), inputs.commandList);
    if (tagResult != sl::Result::eOk)
    {
        return MakeUnavailableEvaluateResult(ToSupportStatus(tagResult));
    }

    const sl::BaseStructure* evaluateInputs[] = {&viewport};
    const sl::Result evaluateResult =
        slEvaluateFeature(sl::kFeatureDLSS, *frameToken, evaluateInputs, _countof(evaluateInputs), inputs.commandList);
    if (evaluateResult != sl::Result::eOk)
    {
        return MakeUnavailableEvaluateResult(ToSupportStatus(evaluateResult));
    }

    return MakeAvailableEvaluateResult();
}

StreamlineDlssOptimalSettingsResult
QueryStreamlineDlssOptimalSettingsWithSdk(const StreamlineDlssOptimalSettingsInputs& inputs)
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

StreamlineDlssDiagnostics QueryStreamlineDlssDiagnosticsWithSdk(const StreamlineDlssOptimalSettingsInputs& inputs)
{
    StreamlineDlssDiagnostics diagnostics;
    diagnostics.sdkMajor = SL_VERSION_MAJOR;
    diagnostics.sdkMinor = SL_VERSION_MINOR;
    diagnostics.sdkPatch = SL_VERSION_PATCH;

    if (!g_streamlineAdapterState.initialized ||
        g_streamlineAdapterState.status != TemporalUpscalerSupportStatus::Available)
    {
        return diagnostics;
    }

    sl::FeatureVersion featureVersion = {};
    if (slGetFeatureVersion(sl::kFeatureDLSS, featureVersion) == sl::Result::eOk)
    {
        diagnostics.featureVersionAvailable = true;
        diagnostics.pluginMajor = featureVersion.versionSL.major;
        diagnostics.pluginMinor = featureVersion.versionSL.minor;
        diagnostics.pluginPatch = featureVersion.versionSL.build;
        diagnostics.ngxMajor = featureVersion.versionNGX.major;
        diagnostics.ngxMinor = featureVersion.versionNGX.minor;
        diagnostics.ngxPatch = featureVersion.versionNGX.build;
    }

    const StreamlineDlssOptimalSettingsResult optimalSettings =
        QueryStreamlineDlssOptimalSettingsWithSdk(inputs);
    if (optimalSettings.available)
    {
        diagnostics.optimalSettingsAvailable = true;
        diagnostics.optimalRenderWidth = optimalSettings.recommendedRenderWidth;
        diagnostics.optimalRenderHeight = optimalSettings.recommendedRenderHeight;
        diagnostics.minRenderWidth = optimalSettings.minRenderWidth;
        diagnostics.minRenderHeight = optimalSettings.minRenderHeight;
        diagnostics.maxRenderWidth = optimalSettings.maxRenderWidth;
        diagnostics.maxRenderHeight = optimalSettings.maxRenderHeight;
    }

    sl::DLSSState state = {};
    if (slDLSSGetState(sl::ViewportHandle(0), state) == sl::Result::eOk)
    {
        diagnostics.stateAvailable = true;
        diagnostics.estimatedVramUsageInBytes = state.estimatedVRAMUsageInBytes;
    }

    return diagnostics;
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

void ShutdownStreamlineAdapterWithoutSdk() {}

TemporalUpscalerSupportInfo QueryStreamlineSupportWithoutSdk()
{
    return MakeSupportInfo(g_streamlineAdapterState.status);
}

StreamlineEvaluateResult EvaluateStreamlineWithoutSdk(const StreamlineEvaluateInputs& inputs)
{
    UNREFERENCED_PARAMETER(inputs);
    return MakeUnavailableEvaluateResult(g_streamlineAdapterState.status);
}

StreamlineDlssOptimalSettingsResult
QueryStreamlineDlssOptimalSettingsWithoutSdk(const StreamlineDlssOptimalSettingsInputs& inputs)
{
    UNREFERENCED_PARAMETER(inputs);
    return MakeUnavailableOptimalSettingsResult(g_streamlineAdapterState.status);
}

StreamlineDlssDiagnostics QueryStreamlineDlssDiagnosticsWithoutSdk(
    const StreamlineDlssOptimalSettingsInputs& inputs)
{
    UNREFERENCED_PARAMETER(inputs);
    return {};
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

StreamlineDlssOptimalSettingsResult
QueryStreamlineDlssOptimalSettings(const StreamlineDlssOptimalSettingsInputs& inputs)
{
#if defined(RTPBRSURVEY_HAS_STREAMLINE_SDK)
    return QueryStreamlineDlssOptimalSettingsWithSdk(inputs);
#else
    return QueryStreamlineDlssOptimalSettingsWithoutSdk(inputs);
#endif
}

StreamlineDlssDiagnostics QueryStreamlineDlssDiagnostics(const StreamlineDlssOptimalSettingsInputs& inputs)
{
#if defined(RTPBRSURVEY_HAS_STREAMLINE_SDK)
    return QueryStreamlineDlssDiagnosticsWithSdk(inputs);
#else
    return QueryStreamlineDlssDiagnosticsWithoutSdk(inputs);
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
