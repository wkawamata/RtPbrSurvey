#include "stdafx.h"

#include "RayReconstructionSpecularHitDistancePass.h"

#include "FullscreenTriangle.h"

#include <pix3.h>

namespace Engine
{

void RecordRayReconstructionSpecularHitDistancePass(ID3D12GraphicsCommandList* commandList)
{
    PIXBeginEvent(commandList, 0, L"RayReconstructionSpecularHitDistancePass");

    DrawFullscreenTriangle(commandList);

    PIXEndEvent(commandList);
}

} // namespace Engine
