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
    switch (status)
    {
        case TemporalUpscalerSupportStatus::NotIntegrated:
            return "SDK not integrated";
        case TemporalUpscalerSupportStatus::Available:
            return "Available";
        case TemporalUpscalerSupportStatus::UnsupportedAdapter:
            return "Unsupported adapter";
        case TemporalUpscalerSupportStatus::MissingRuntime:
            return "Runtime missing";
        case TemporalUpscalerSupportStatus::InitializationFailed:
            return "Initialization failed";
        default:
            return "Unknown";
    }
}

TemporalUpscalerSupportInfo TemporalUpscalerSupportInfo::Create()
{
    return QueryStreamlineSupport();
}

} // namespace Engine
