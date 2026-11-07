#pragma once

#include <array>
#include <chrono>
#include <cstddef>

namespace RtPbrSurvey
{
    enum class RendererCpuPhase { Update, RecordSubmit, GpuWait, Present, Count };
    inline constexpr std::size_t kRendererCpuPhaseCount = static_cast<std::size_t>(RendererCpuPhase::Count);
    inline constexpr std::array<const char*, kRendererCpuPhaseCount> kRendererCpuPhaseNames = {
        "Update / housekeeping", "Record / submit", "GPU fence wait", "Present"
    };

    struct RendererCpuTiming
    {
        bool available = false;
        float totalMs = 0;
        std::array<float, kRendererCpuPhaseCount> ms = {};
    };

    // One exclusive CPU timeline. Nested scopes temporarily replace the parent phase.
    // Callers can inject timestamps for deterministic tests; no GPU wait/query is added.
    class RendererCpuTimer
    {
    public:
        using Clock = std::chrono::steady_clock;
        using TimePoint = Clock::time_point;
        void Begin(TimePoint now = Clock::now())
        {
            m_timing = {};
            m_phase = RendererCpuPhase::Update;
            m_previous = now;
            m_active = true;
        }
        RendererCpuPhase Switch(RendererCpuPhase phase, TimePoint now = Clock::now())
        {
            const auto previous = m_phase;
            if (m_active)
            {
                Accumulate(now);
                m_phase = phase;
            }
            return previous;
        }
        void End(TimePoint now = Clock::now())
        {
            if (!m_active) return;
            Accumulate(now);
            m_timing.totalMs = 0;
            for (const auto ms : m_timing.ms) m_timing.totalMs += ms;
            m_timing.available = true;
            m_active = false;
        }
        const RendererCpuTiming& Completed() const { return m_timing; }
    private:
        void Accumulate(TimePoint now)
        {
            if (now < m_previous) return;
            m_timing.ms[static_cast<std::size_t>(m_phase)] +=
                std::chrono::duration<float, std::milli>(now - m_previous).count();
            m_previous = now;
        }
        RendererCpuTiming m_timing;
        RendererCpuPhase m_phase = RendererCpuPhase::Update;
        TimePoint m_previous = {};
        bool m_active = false;
    };

    class ScopedRendererCpuPhase
    {
    public:
        ScopedRendererCpuPhase(RendererCpuTimer& timer, RendererCpuPhase phase)
            : m_timer(timer), m_previous(timer.Switch(phase)) {}
        ~ScopedRendererCpuPhase() { m_timer.Switch(m_previous); }
        ScopedRendererCpuPhase(const ScopedRendererCpuPhase&) = delete;
        ScopedRendererCpuPhase& operator=(const ScopedRendererCpuPhase&) = delete;
    private:
        RendererCpuTimer& m_timer;
        RendererCpuPhase m_previous;
    };
}
