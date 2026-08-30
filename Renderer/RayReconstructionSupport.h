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

    const char* StatusText() const;
};

} // namespace Engine
