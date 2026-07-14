#pragma once

namespace Engine
{

enum class TemporalUpscalerBackend
{
    None = 0,
    Streamline,
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

    static TemporalUpscalerSupportInfo Create();
};

} // namespace Engine
