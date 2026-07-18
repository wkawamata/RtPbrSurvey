#pragma once

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
};

struct StreamlineEvaluateResult
{
    bool outputAvailable = false;
    TemporalUpscalerSupportStatus status = TemporalUpscalerSupportStatus::NotIntegrated;
};

struct StreamlineDlssOptimalSettingsInputs
{
    std::uint32_t outputWidth = 0;
    std::uint32_t outputHeight = 0;
    TemporalUpscalerQualityMode qualityMode = TemporalUpscalerQualityMode::Native;
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

TemporalUpscalerSupportInfo InitializeStreamlineAdapter(const StreamlineAdapterInitDesc& desc);
void ShutdownStreamlineAdapter();
TemporalUpscalerSupportInfo QueryStreamlineSupport();
StreamlineDlssOptimalSettingsResult QueryStreamlineDlssOptimalSettings(
    const StreamlineDlssOptimalSettingsInputs& inputs);
StreamlineEvaluateResult EvaluateStreamline(const StreamlineEvaluateInputs& inputs);

} // namespace Engine
