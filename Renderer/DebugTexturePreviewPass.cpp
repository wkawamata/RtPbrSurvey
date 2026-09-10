#include "stdafx.h"

#include "DebugTexturePreviewPass.h"

#include "FullscreenTriangle.h"

#include <pix3.h>

namespace Engine
{
auto DebugTexturePreviewSettings::MakeShaderConstants(
    float cameraNear, float cameraFar, bool orthographicProjection) const -> ShaderConstants
{
    return {static_cast<UINT>(semantic),
            static_cast<UINT>(channel),
            exposure,
            scale,
            offset,
            nearestSampling ? 1u : 0u,
            depthVisualization.MakeShaderConstants(cameraNear, cameraFar, orthographicProjection),
            static_cast<UINT>(bufferVisualizationMode),
            bufferWidth,
            bufferHeight,
            bufferRowStrideElements,
            bufferElementStride,
            bufferComponentOffsetBytes,
            bufferComponentCount,
            bufferComponentType};
}

void RecordDebugTexturePreviewPass(ID3D12GraphicsCommandList* commandList)
{
    PIXBeginEvent(commandList, 0, L"DebugTexturePreviewPass");
    DrawFullscreenTriangle(commandList);
    PIXEndEvent(commandList);
}
} // namespace Engine
