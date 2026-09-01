#pragma once

#include "RayReconstructionSupport.h"
#include "TemporalUpscalerSupport.h"

#include <cstdint>
#include <d3d12.h>

namespace Engine
{

struct StreamlineAdapterInitDesc
{
    const wchar_t* applicationName = nullptr;
};

struct StreamlineEvaluateInputs
{
    ID3D12GraphicsCommandList* commandList = nullptr;
    ID3D12Resource* inputSceneColor = nullptr;
    ID3D12Resource* depth = nullptr;
    ID3D12Resource* motionVectors = nullptr;
    ID3D12Resource* outputSceneColor = nullptr;
    TemporalUpscalerSettings settings;
    std::uint32_t renderWidth = 0;
    std::uint32_t renderHeight = 0;
    std::uint32_t outputWidth = 0;
    std::uint32_t outputHeight = 0;
    bool historyReset = false;
    TemporalUpscalerFrameConstants frameConstants;
};

struct StreamlineEvaluateResult
{
    bool outputAvailable = false;
    TemporalUpscalerSupportStatus status = TemporalUpscalerSupportStatus::NotIntegrated;
    const char* failureStage = nullptr;
    std::int32_t nativeResult = 0;
};

struct RayReconstructionFrameConstants
{
    TemporalUpscalerFrameConstants temporal;
    std::array<float, 16> worldToCameraView = {};
    std::array<float, 16> cameraViewToWorld = {};
    std::array<float, 2> motionVectorScale = {1.0f, 1.0f};
    std::array<float, 2> motionVectorValueOffset = {};
    float preExposure = 1.0f;
    float exposureScale = 1.0f;
};

struct RayReconstructionEvaluateInputs
{
    ID3D12GraphicsCommandList* commandList = nullptr;
    ID3D12Resource* scalingInputColor = nullptr;
    ID3D12Resource* reflectionEvaluatedRadiance = nullptr;
    ID3D12Resource* reflectionResolvedRadiance = nullptr;
    ID3D12Resource* depth = nullptr;
    ID3D12Resource* motionVectors = nullptr;
    ID3D12Resource* normal = nullptr;
    ID3D12Resource* roughness = nullptr;
    ID3D12Resource* albedo = nullptr;
    ID3D12Resource* specularAlbedo = nullptr;
    ID3D12Resource* specularHitDistance = nullptr;
    TemporalUpscalerQualityMode qualityMode = TemporalUpscalerQualityMode::Native;
    TemporalUpscalerPreset preset = TemporalUpscalerPreset::Default;
    std::uint32_t renderWidth = 0;
    std::uint32_t renderHeight = 0;
    bool historyReset = false;
    bool historyValid = false;
    bool enableNativeEvaluation = false;
    RayReconstructionFrameConstants frameConstants;
};

struct RayReconstructionEvaluateResult
{
    bool outputAvailable = false;
    RayReconstructionSupportStatus status = RayReconstructionSupportStatus::NotIntegrated;
    bool inputReady = false;
    RayReconstructionReadinessReason inputReadinessReason =
        RayReconstructionReadinessReason::NativeEvaluationDisabled;
};

struct StreamlineDlssOptimalSettingsInputs
{
    std::uint32_t outputWidth = 0;
    std::uint32_t outputHeight = 0;
    TemporalUpscalerQualityMode qualityMode = TemporalUpscalerQualityMode::Native;
    TemporalUpscalerPreset preset = TemporalUpscalerPreset::Default;
};

struct StreamlineDlssOptimalSettingsResult
{
    bool available = false;
    TemporalUpscalerSupportStatus status = TemporalUpscalerSupportStatus::NotIntegrated;
    std::uint32_t recommendedRenderWidth = 0;
    std::uint32_t recommendedRenderHeight = 0;
    std::uint32_t minRenderWidth = 0;
    std::uint32_t minRenderHeight = 0;
    std::uint32_t maxRenderWidth = 0;
    std::uint32_t maxRenderHeight = 0;
};

struct StreamlineDlssDiagnostics
{
    bool featureVersionAvailable = false;
    std::uint32_t sdkMajor = 0;
    std::uint32_t sdkMinor = 0;
    std::uint32_t sdkPatch = 0;
    std::uint32_t pluginMajor = 0;
    std::uint32_t pluginMinor = 0;
    std::uint32_t pluginPatch = 0;
    std::uint32_t ngxMajor = 0;
    std::uint32_t ngxMinor = 0;
    std::uint32_t ngxPatch = 0;
    bool optimalSettingsAvailable = false;
    std::uint32_t optimalRenderWidth = 0;
    std::uint32_t optimalRenderHeight = 0;
    std::uint32_t minRenderWidth = 0;
    std::uint32_t minRenderHeight = 0;
    std::uint32_t maxRenderWidth = 0;
    std::uint32_t maxRenderHeight = 0;
    bool stateAvailable = false;
    std::uint64_t estimatedVramUsageInBytes = 0;
};

TemporalUpscalerSupportInfo InitializeStreamlineAdapter(const StreamlineAdapterInitDesc& desc);
TemporalUpscalerSupportInfo SetStreamlineD3DDevice(ID3D12Device* device);
void ShutdownStreamlineAdapter();
TemporalUpscalerSupportInfo QueryStreamlineSupport();
RayReconstructionSupportInfo QueryStreamlineRayReconstructionSupport();
StreamlineDlssOptimalSettingsResult QueryStreamlineDlssOptimalSettings(
    const StreamlineDlssOptimalSettingsInputs& inputs);
StreamlineDlssDiagnostics QueryStreamlineDlssDiagnostics(const StreamlineDlssOptimalSettingsInputs& inputs);
RayReconstructionDiagnostics QueryStreamlineRayReconstructionDiagnostics();
RayReconstructionEvaluateResult EvaluateStreamlineRayReconstruction(
    const RayReconstructionEvaluateInputs& inputs);
StreamlineEvaluateResult EvaluateStreamline(const StreamlineEvaluateInputs& inputs);

} // namespace Engine
