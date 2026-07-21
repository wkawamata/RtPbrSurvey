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

float TemporalUpscalerSettings::ClampedSharpness() const
{
    return std::clamp(sharpness, kMinSharpness, kMaxSharpness);
}

std::array<float, 2> TemporalUpscalerSettings::ClampedJitterScale() const
{
    return {std::clamp(jitterScale[0], kMinJitterScale, kMaxJitterScale),
            std::clamp(jitterScale[1], kMinJitterScale, kMaxJitterScale)};
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
        case TemporalUpscalerSupportStatus::DeviceNotSet:
            return "D3D12 device not set";
        case TemporalUpscalerSupportStatus::DriverOutOfDate:
            return "Driver out of date";
        case TemporalUpscalerSupportStatus::OperatingSystemOutOfDate:
            return "Operating system out of date";
        case TemporalUpscalerSupportStatus::HardwareSchedulingDisabled:
            return "Hardware scheduling disabled";
        case TemporalUpscalerSupportStatus::InvalidIntegration:
            return "Invalid integration";
        default:
            return "Unknown";
    }
}

TemporalUpscalerSupportInfo TemporalUpscalerSupportInfo::Create()
{
    return QueryStreamlineSupport();
}

} // namespace Engine
