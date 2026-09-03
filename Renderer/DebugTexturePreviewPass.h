#pragma once

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
    };

    DebugTexturePreviewSemantic semantic = DebugTexturePreviewSemantic::Color;
    DebugTexturePreviewChannel channel = DebugTexturePreviewChannel::Rgba;
    float exposure = 0.0f;
    float scale = 1.0f;
    float offset = 0.0f;
    bool nearestSampling = true;

    ShaderConstants MakeShaderConstants() const;
};

void RecordDebugTexturePreviewPass(ID3D12GraphicsCommandList* commandList);
} // namespace Engine
