#include "stdafx.h"

#include "ReflectionEvaluatePass.h"

#include "FullscreenTriangle.h"

#include <pix3.h>

namespace Engine
{

void RecordReflectionEvaluatePass(ID3D12GraphicsCommandList* commandList)
{
    PIXBeginEvent(commandList, 0, L"ReflectionEvaluatePass");

    DrawFullscreenTriangle(commandList);

    PIXEndEvent(commandList);
}

} // namespace Engine
