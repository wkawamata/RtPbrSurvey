#pragma once

#include <array>
#include <cstdint>

namespace Engine
{

enum class TemporalUpscalerBackend
{
    None = 0,
    Streamline,
};

enum class TemporalUpscalerSupportStatus
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

enum class TemporalUpscalerQualityMode
{
    Native = 0,
    UltraQuality,
    Quality,
    Balanced,
    Performance,
    UltraPerformance,
};

struct TemporalUpscalerSettings
{
    static constexpr float kMinRenderScale = 0.25f;
    static constexpr float kMaxRenderScale = 1.0f;
    static constexpr float kMinSharpness = 0.0f;
    static constexpr float kMaxSharpness = 1.0f;
    static constexpr float kMinJitterScale = -2.0f;
    static constexpr float kMaxJitterScale = 2.0f;

    bool enabled = false;
    TemporalUpscalerBackend backend = TemporalUpscalerBackend::Streamline;
    TemporalUpscalerQualityMode qualityMode = TemporalUpscalerQualityMode::Native;
    float renderScale = 1.0f;
    float sharpness = 0.0f;
    std::array<float, 2> jitterScale = {1.0f, 1.0f};
    std::array<float, 2> motionVectorScale = {1.0f, 1.0f};
    std::array<float, 2> motionVectorValueOffset = {};
    bool autoExposure = true;

    float ClampedRenderScale() const;
    float ClampedSharpness() const;
    std::array<float, 2> ClampedJitterScale() const;
};

struct TemporalUpscalerFrameConstants
{
    std::array<float, 16> cameraViewToClip = {};
    std::array<float, 16> clipToCameraView = {};
    std::array<float, 16> clipToPrevClip = {};
    std::array<float, 16> prevClipToClip = {};
    std::array<float, 3> cameraPosition = {};
    std::array<float, 3> cameraUp = {};
    std::array<float, 3> cameraRight = {};
    std::array<float, 3> cameraForward = {};
    std::array<float, 2> jitterOffset = {};
    float cameraNear = 0.0f;
    float cameraFar = 0.0f;
    float cameraFovRadians = 0.0f;
    float cameraAspectRatio = 1.0f;
    bool depthInverted = false;
    bool cameraMotionIncluded = true;
};

struct TemporalUpscalerSupportInfo
{
    bool IsAvailable() const
    {
        return available;
    }

    const char* BackendName() const;
    const char* StatusText() const;

    bool available = false;
    TemporalUpscalerBackend backend = TemporalUpscalerBackend::None;
    TemporalUpscalerSupportStatus status = TemporalUpscalerSupportStatus::NotIntegrated;

    static TemporalUpscalerSupportInfo Create();
};

} // namespace Engine
