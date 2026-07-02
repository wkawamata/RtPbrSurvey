#include "stdafx.h"

#include "ReflectionRayHitDebugPass.h"

#include "FullscreenTriangle.h"

#include <pix3.h>

namespace Engine
{

void RecordReflectionRayHitDebugPass(ID3D12GraphicsCommandList* commandList)
{
    PIXBeginEvent(commandList, 0, L"ReflectionRayHitDebugPass");

    DrawFullscreenTriangle(commandList);

    PIXEndEvent(commandList);
}

} // namespace Engine
