#include "stdafx.h"

#include "EdgeAwareSpatialReflectionPass.h"

#include "FullscreenTriangle.h"

#include <pix3.h>

namespace Engine
{

void RecordEdgeAwareSpatialReflectionPass(ID3D12GraphicsCommandList* commandList)
{
    PIXBeginEvent(commandList, 0, L"EdgeAwareSpatialReflectionPass");

    DrawFullscreenTriangle(commandList);

    PIXEndEvent(commandList);
}

} // namespace Engine
