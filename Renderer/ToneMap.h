#pragma once

#include "HdrOutput.h"

#include <algorithm>
#include <d3d12.h>

namespace Engine
{

static constexpr UINT kDefaultToneMapOperator = 0; // 0: None, 1: Reinhard, 2: ACES
static constexpr float kDefaultExposure = 1.0f;
static constexpr float kDefaultPaperWhiteNits = 300.0f;
static constexpr float kDefaultMaxDisplayNits = 1000.0f;

struct MaterialHighlightSettings
{
    bool enabled = false;
    UINT materialId = 0;
    bool complementary = false;
    float opacity = 0.5f;
    float color[3] = {1.0f, 0.25f, 0.0f};
};

struct ToneMapSettings
{
    struct ShaderConstants
    {
        UINT toneMapOperator;
        UINT transferFunction;
        float exposure;
        float paperWhiteNits;
        float maxDisplayNits;
        UINT nearestSampling;
        UINT highlightEnabled;
        UINT highlightMaterialId;
        UINT highlightComplementary;
        float highlightOpacity;
        float highlightColor[3];
    };

    int operatorIndex = kDefaultToneMapOperator;
    float exposure = kDefaultExposure;
    float paperWhiteNits = kDefaultPaperWhiteNits;
    float maxDisplayNits = kDefaultMaxDisplayNits;
    MaterialHighlightSettings highlight;

    void Normalize()
    {
        operatorIndex = std::clamp(operatorIndex, 0, 2);
        exposure = (std::max)(exposure, 0.0f);
        paperWhiteNits = (std::max)(paperWhiteNits, 1.0f);
        maxDisplayNits = (std::max)(maxDisplayNits, paperWhiteNits);
    }

    ShaderConstants MakeShaderConstants(UINT transferFunction, bool nearestSampling) const
    {
        return {static_cast<UINT>(operatorIndex),
                transferFunction,
                exposure,
                paperWhiteNits,
                maxDisplayNits,
                nearestSampling ? 1u : 0u,
                highlight.enabled ? 1u : 0u,
                highlight.materialId,
                highlight.complementary ? 1u : 0u,
                highlight.opacity,
                {highlight.color[0], highlight.color[1], highlight.color[2]}};
    }
};

struct ToneMapPass
{
    ToneMapSettings settings;

    ToneMapSettings::ShaderConstants MakeShaderConstants(const HdrOutputSettings& hdrOutputSettings,
                                                         bool nearestSampling) const;
};

void RecordToneMapPass(ID3D12GraphicsCommandList* commandList);

} // namespace Engine
