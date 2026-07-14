#include "stdafx.h"

#include "TemporalUpscalerSupport.h"

namespace Engine
{

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
    TemporalUpscalerSupportInfo info;
    return info;
}

} // namespace Engine
