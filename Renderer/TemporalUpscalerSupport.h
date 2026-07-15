#pragma once

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
};

struct TemporalUpscalerSettings
{
    static constexpr float kMinRenderScale = 0.25f;
    static constexpr float kMaxRenderScale = 1.0f;

    bool enabled = false;
    float renderScale = 1.0f;

    float ClampedRenderScale() const;
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
