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

enum class DebugBufferVisualizationMode : UINT
{
    Image,
    Heatmap,
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
        UINT bufferVisualizationMode;
        UINT bufferWidth;
        UINT bufferHeight;
        UINT bufferRowStrideElements;
        UINT bufferElementStride;
        UINT bufferComponentOffsetBytes;
        UINT bufferComponentCount;
        UINT bufferComponentType;
    };

    DebugTexturePreviewSemantic semantic = DebugTexturePreviewSemantic::Color;
    DebugTexturePreviewChannel channel = DebugTexturePreviewChannel::Rgba;
    float exposure = 0.0f;
    float scale = 1.0f;
    float offset = 0.0f;
    bool nearestSampling = true;
    DepthVisualizationSettings depthVisualization;
    DebugBufferVisualizationMode bufferVisualizationMode = DebugBufferVisualizationMode::Image;
    UINT bufferWidth = 0;
    UINT bufferHeight = 0;
    UINT bufferRowStrideElements = 0;
    UINT bufferElementStride = 0;
    UINT bufferComponentOffsetBytes = 0;
    UINT bufferComponentCount = 0;
    UINT bufferComponentType = 0;

    ShaderConstants MakeShaderConstants(float cameraNear, float cameraFar, bool orthographicProjection) const;
};

static_assert(sizeof(DebugTexturePreviewSettings::ShaderConstants) == 22 * sizeof(UINT));

void RecordDebugTexturePreviewPass(ID3D12GraphicsCommandList* commandList);
} // namespace Engine
