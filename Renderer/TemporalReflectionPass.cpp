#include "stdafx.h"

#include "TemporalReflectionPass.h"

#include "FullscreenTriangle.h"

#include <pix3.h>

namespace Engine
{

void RecordTemporalReflectionPass(ID3D12GraphicsCommandList* commandList)
{
    PIXBeginEvent(commandList, 0, L"TemporalReflectionPass");

    DrawFullscreenTriangle(commandList);

    PIXEndEvent(commandList);
}

} // namespace Engine
