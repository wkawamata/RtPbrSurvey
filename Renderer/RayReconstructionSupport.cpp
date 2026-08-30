#include "stdafx.h"

#include "RayReconstructionSupport.h"

#include "StreamlineAdapter.h"

namespace Engine
{

const char* RayReconstructionSupportInfo::BackendName() const
{
    switch (backend)
    {
        case RayReconstructionBackend::None:
            return "None";
        case RayReconstructionBackend::Streamline:
            return "Streamline";
        default:
            return "Unknown";
    }
}

const char* RayReconstructionSupportInfo::StatusText() const
{
    switch (status)
    {
        case RayReconstructionSupportStatus::NotIntegrated:
            return "SDK not integrated";
        case RayReconstructionSupportStatus::Available:
            return "Available";
        case RayReconstructionSupportStatus::UnsupportedAdapter:
            return "Unsupported adapter";
        case RayReconstructionSupportStatus::MissingRuntime:
            return "Runtime missing";
        case RayReconstructionSupportStatus::InitializationFailed:
            return "Initialization failed";
        case RayReconstructionSupportStatus::DeviceNotSet:
            return "D3D12 device not set";
        case RayReconstructionSupportStatus::DriverOutOfDate:
            return "Driver out of date";
        case RayReconstructionSupportStatus::OperatingSystemOutOfDate:
            return "Operating system out of date";
        case RayReconstructionSupportStatus::HardwareSchedulingDisabled:
            return "Hardware scheduling disabled";
        case RayReconstructionSupportStatus::InvalidIntegration:
            return "Invalid integration";
        default:
            return "Unknown";
    }
}

RayReconstructionSupportInfo RayReconstructionSupportInfo::Create()
{
    return QueryStreamlineRayReconstructionSupport();
}

const char* RayReconstructionDiagnostics::StatusText() const
{
    RayReconstructionSupportInfo info;
    info.status = status;
    return info.StatusText();
}

} // namespace Engine
