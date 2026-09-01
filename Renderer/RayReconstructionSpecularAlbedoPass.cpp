#include "stdafx.h"

#include "RayReconstructionSpecularAlbedoPass.h"

#include "FullscreenTriangle.h"

#include <pix3.h>

namespace Engine
{

void RecordRayReconstructionSpecularAlbedoPass(ID3D12GraphicsCommandList* commandList)
{
    PIXBeginEvent(commandList, 0, L"RayReconstructionSpecularAlbedoPass");

    DrawFullscreenTriangle(commandList);

    PIXEndEvent(commandList);
}

} // namespace Engine
