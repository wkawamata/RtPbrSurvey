#pragma once

#include "TemporalUpscalerSupport.h"

#include <cstdint>
#include <d3d12.h>

namespace Engine
{

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

TemporalUpscalerSupportInfo QueryStreamlineSupport();
StreamlineEvaluateResult EvaluateStreamline(const StreamlineEvaluateInputs& inputs);

} // namespace Engine
