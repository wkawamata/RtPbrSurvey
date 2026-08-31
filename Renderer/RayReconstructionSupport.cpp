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

const char* RayReconstructionDiagnostics::InputReadinessText() const
{
    return ToString(inputReadinessReason);
}

const char* RayReconstructionDiagnostics::LastEvaluateStatusText() const
{
    RayReconstructionSupportInfo info;
    info.status = lastEvaluateStatus;
    return info.StatusText();
}

const char* ToString(RayReconstructionReadinessReason reason)
{
    switch (reason)
    {
        case RayReconstructionReadinessReason::Ready:
            return "Ready";
        case RayReconstructionReadinessReason::NativeEvaluationDisabled:
            return "Native evaluation disabled";
        case RayReconstructionReadinessReason::MissingCommandList:
            return "Missing command list";
        case RayReconstructionReadinessReason::MissingReflectionEvaluatedRadiance:
            return "Missing reflection evaluated radiance";
        case RayReconstructionReadinessReason::MissingReflectionResolvedRadiance:
            return "Missing reflection resolved radiance";
        case RayReconstructionReadinessReason::MissingScalingInputColor:
            return "Missing scaling input color";
        case RayReconstructionReadinessReason::MissingDepth:
            return "Missing depth";
        case RayReconstructionReadinessReason::MissingMotionVectors:
            return "Missing motion vectors";
        case RayReconstructionReadinessReason::MissingNormal:
            return "Missing normal";
        case RayReconstructionReadinessReason::MissingRoughness:
            return "Missing roughness";
        case RayReconstructionReadinessReason::MissingAlbedo:
            return "Missing albedo";
        case RayReconstructionReadinessReason::MissingSpecularAlbedo:
            return "Missing specular albedo";
        case RayReconstructionReadinessReason::MissingSpecularHitDistance:
            return "Missing specular hit distance";
        case RayReconstructionReadinessReason::InvalidRenderSize:
            return "Invalid render size";
        case RayReconstructionReadinessReason::InvalidScalingInputColorFormat:
            return "Invalid scaling input color format";
        case RayReconstructionReadinessReason::InvalidReflectionEvaluatedRadianceFormat:
            return "Invalid reflection evaluated radiance format";
        case RayReconstructionReadinessReason::InvalidReflectionResolvedRadianceFormat:
            return "Invalid reflection resolved radiance format";
        case RayReconstructionReadinessReason::InvalidDepthFormat:
            return "Invalid depth format";
        case RayReconstructionReadinessReason::InvalidMotionVectorFormat:
            return "Invalid motion vector format";
        case RayReconstructionReadinessReason::InvalidNormalFormat:
            return "Invalid normal format";
        case RayReconstructionReadinessReason::InvalidRoughnessFormat:
            return "Invalid roughness format";
        case RayReconstructionReadinessReason::InvalidAlbedoFormat:
            return "Invalid albedo format";
        case RayReconstructionReadinessReason::InvalidSpecularAlbedoFormat:
            return "Invalid specular albedo format";
        case RayReconstructionReadinessReason::InvalidSpecularHitDistanceFormat:
            return "Invalid specular hit distance format";
        case RayReconstructionReadinessReason::MissingCameraConstants:
            return "Missing camera constants";
        default:
            return "Unknown";
    }
}

} // namespace Engine
