#include "stdafx.h"

#include "TemporalUpscalerSupport.h"

#include <algorithm>

#include "StreamlineAdapter.h"

namespace Engine
{

float TemporalUpscalerSettings::ClampedRenderScale() const
{
    return std::clamp(renderScale, kMinRenderScale, kMaxRenderScale);
}

const char* TemporalUpscalerSupportInfo::BackendName() const
{
    switch (backend)
    {
        case TemporalUpscalerBackend::None:
            return "None";
        case TemporalUpscalerBackend::Streamline:
            return "Streamline";
        default:
            return "Unknown";
    }
}

const char* TemporalUpscalerSupportInfo::StatusText() const
{
    if (!available)
    {
        return "SDK not integrated";
    }

    return "Available";
}

TemporalUpscalerSupportInfo TemporalUpscalerSupportInfo::Create()
{
    return QueryStreamlineSupport();
}

} // namespace Engine
