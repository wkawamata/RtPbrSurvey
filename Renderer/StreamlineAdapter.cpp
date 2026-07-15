#include "stdafx.h"

#include "StreamlineAdapter.h"

namespace Engine
{

TemporalUpscalerSupportInfo QueryStreamlineSupport()
{
    TemporalUpscalerSupportInfo info;
    info.backend = TemporalUpscalerBackend::Streamline;
    info.available = false;
    return info;
}

} // namespace Engine
