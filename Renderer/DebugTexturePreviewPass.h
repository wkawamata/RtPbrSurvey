#pragma once

#include "DepthVisualization.h"

#include <d3d12.h>

namespace Engine
{
enum class DebugTexturePreviewSemantic : UINT
{
    Color,
    Normal,
    Depth,
    MotionVector,
    Scalar,
};

enum class DebugTexturePreviewChannel : UINT
{
    Rgba,
    R,
    G,
    B,
    A,
};

struct DebugTexturePreviewSettings
{
    struct ShaderConstants
    {
        UINT semantic;
        UINT channel;
        float exposure;
        float scale;
        float offset;
        UINT nearestSampling;
        DepthVisualizationShaderConstants depthVisualization;
    };

    DebugTexturePreviewSemantic semantic = DebugTexturePreviewSemantic::Color;
    DebugTexturePreviewChannel channel = DebugTexturePreviewChannel::Rgba;
    float exposure = 0.0f;
    float scale = 1.0f;
    float offset = 0.0f;
    bool nearestSampling = true;
    DepthVisualizationSettings depthVisualization;

    ShaderConstants MakeShaderConstants(float cameraNear, float cameraFar, bool orthographicProjection) const;
};

static_assert(sizeof(DebugTexturePreviewSettings::ShaderConstants) == 14 * sizeof(UINT));

void RecordDebugTexturePreviewPass(ID3D12GraphicsCommandList* commandList);
} // namespace Engine
