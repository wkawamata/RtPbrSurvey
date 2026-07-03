#pragma once

#include <d3d12.h>

namespace Engine
{

void RecordLightingPass(ID3D12GraphicsCommandList* commandList);
void RecordLightingDebugGradientPass(ID3D12GraphicsCommandList* commandList);

} // namespace Engine
