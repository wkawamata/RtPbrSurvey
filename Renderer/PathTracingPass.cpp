#include "stdafx.h"

#include "PathTracingPass.h"

#include <pix3.h>

namespace Engine
{

void RecordPathTracingUavClear(ID3D12GraphicsCommandList* commandList,
                               const PathTracingUavClearDesc& desc,
                               const wchar_t* eventName)
{
    assert(commandList != nullptr);
    assert(desc.resource != nullptr);
    assert(desc.clearColor != nullptr);

    PIXBeginEvent(commandList, 0, eventName);
    commandList->ClearUnorderedAccessViewFloat(
        desc.gpuUav, desc.cpuUav, desc.resource, desc.clearColor, 0, nullptr);
    PIXEndEvent(commandList);
}

} // namespace Engine
