#pragma once

#include <cstdint>

namespace Engine
{

enum class RayReconstructionBackend
{
    None = 0,
    Streamline,
};

enum class RayReconstructionSupportStatus
{
    NotIntegrated = 0,
    Available,
    UnsupportedAdapter,
    MissingRuntime,
    InitializationFailed,
    DeviceNotSet,
    DriverOutOfDate,
    OperatingSystemOutOfDate,
    HardwareSchedulingDisabled,
    InvalidIntegration,
};

enum class RayReconstructionReadinessReason
{
    Ready = 0,
    NativeEvaluationDisabled,
    MissingCommandList,
    MissingReflectionEvaluatedRadiance,
    MissingReflectionResolvedRadiance,
    MissingScalingInputColor,
    MissingDepth,
    MissingMotionVectors,
    MissingNormal,
    MissingRoughness,
    MissingAlbedo,
    MissingSpecularAlbedo,
    MissingSpecularHitDistance,
    InvalidRenderSize,
    InvalidScalingInputColorFormat,
    InvalidReflectionEvaluatedRadianceFormat,
    InvalidReflectionResolvedRadianceFormat,
    InvalidDepthFormat,
    InvalidMotionVectorFormat,
    InvalidNormalFormat,
    InvalidRoughnessFormat,
    InvalidAlbedoFormat,
    InvalidSpecularAlbedoFormat,
    InvalidSpecularHitDistanceFormat,
    MissingCameraConstants,
};

struct RayReconstructionSettings
{
    bool enabled = false;
    RayReconstructionBackend backend = RayReconstructionBackend::Streamline;
};

struct RayReconstructionSupportInfo
{
    bool IsAvailable() const
    {
        return available;
    }

    const char* BackendName() const;
    const char* StatusText() const;

    bool available = false;
    RayReconstructionBackend backend = RayReconstructionBackend::None;
    RayReconstructionSupportStatus status = RayReconstructionSupportStatus::NotIntegrated;

    static RayReconstructionSupportInfo Create();
};

struct RayReconstructionDiagnostics
{
    RayReconstructionSupportStatus status = RayReconstructionSupportStatus::NotIntegrated;
    bool featureVersionAvailable = false;
    std::uint32_t sdkMajor = 0;
    std::uint32_t sdkMinor = 0;
    std::uint32_t sdkPatch = 0;
    std::uint32_t pluginMajor = 0;
    std::uint32_t pluginMinor = 0;
    std::uint32_t pluginPatch = 0;
    std::uint32_t ngxMajor = 0;
    std::uint32_t ngxMinor = 0;
    std::uint32_t ngxPatch = 0;
    bool inputReadinessAvailable = false;
    bool inputReady = false;
    RayReconstructionReadinessReason inputReadinessReason = RayReconstructionReadinessReason::NativeEvaluationDisabled;

    const char* StatusText() const;
    const char* InputReadinessText() const;
};

const char* ToString(RayReconstructionReadinessReason reason);

} // namespace Engine
