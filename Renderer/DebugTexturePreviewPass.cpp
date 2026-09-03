#include "stdafx.h"

#include "DebugTexturePreviewPass.h"

#include "FullscreenTriangle.h"

#include <pix3.h>

namespace Engine
{
auto DebugTexturePreviewSettings::MakeShaderConstants() const -> ShaderConstants
{
    return {static_cast<UINT>(semantic),
            static_cast<UINT>(channel),
            exposure,
            scale,
            offset,
            nearestSampling ? 1u : 0u};
}

void RecordDebugTexturePreviewPass(ID3D12GraphicsCommandList* commandList)
{
    PIXBeginEvent(commandList, 0, L"DebugTexturePreviewPass");
    DrawFullscreenTriangle(commandList);
    PIXEndEvent(commandList);
}
} // namespace Engine
