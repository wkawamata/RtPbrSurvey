#pragma once

#include "Runtime/CaptureSession.h"

#include <algorithm>
#include <stdexcept>

namespace RtPbrSurvey
{
enum class CaptureFrameUpdateMode
{
    LiveScene,
    FixedCapture,
    Paused,
    CapacityWait,
    DrainOnly,
};

// Host-side update clock. Completion polling and presentation never advance it.
class CaptureFrameClock
{
public:
    void Reset(CaptureSessionClock clock, unsigned int framesPerSecond)
    {
        if (framesPerSecond == 0)
            throw std::invalid_argument("Capture frame clock requires a nonzero FPS.");
        m_clock = clock;
        m_framesPerSecond = framesPerSecond;
        m_sceneUpdates = 0;
        m_fixedUpdates = 0;
        m_liveSimulationSeconds = 0.0;
    }

    CaptureFrameUpdateMode
    ChooseUpdateMode(CaptureSessionState state, bool sceneUpdateRequested, bool captureCapacityAvailable) const
    {
        if (m_clock == CaptureSessionClock::FixedStep &&
            (state == CaptureSessionState::Draining || state == CaptureSessionState::Finalizing))
            return CaptureFrameUpdateMode::DrainOnly;
        if (!sceneUpdateRequested)
            return CaptureFrameUpdateMode::Paused;
        if (m_clock == CaptureSessionClock::FixedStep &&
            (state == CaptureSessionState::Warmup || state == CaptureSessionState::Recording))
            return captureCapacityAvailable ? CaptureFrameUpdateMode::FixedCapture
                                            : CaptureFrameUpdateMode::CapacityWait;
        // Start delay remains wall-time driven; the live scene can continue during it.
        return CaptureFrameUpdateMode::LiveScene;
    }

    static bool AdvancesScene(CaptureFrameUpdateMode mode)
    {
        return mode == CaptureFrameUpdateMode::LiveScene || mode == CaptureFrameUpdateMode::FixedCapture;
    }

    float AdvanceSceneUpdate(CaptureFrameUpdateMode mode, float liveDeltaSeconds)
    {
        if (!AdvancesScene(mode))
            return 0.0f;
        ++m_sceneUpdates;
        if (mode == CaptureFrameUpdateMode::FixedCapture)
        {
            ++m_fixedUpdates;
            return 1.0f / static_cast<float>(m_framesPerSecond);
        }
        const float deltaSeconds = (std::max)(0.0f, liveDeltaSeconds);
        m_liveSimulationSeconds += deltaSeconds;
        return deltaSeconds;
    }

    double GetSimulationSeconds() const
    {
        return m_clock == CaptureSessionClock::FixedStep ? static_cast<double>(m_fixedUpdates) / m_framesPerSecond
                                                         : m_liveSimulationSeconds;
    }

    CaptureSessionTiming MakeTiming(double realSeconds) const
    {
        return {m_sceneUpdates, realSeconds, GetSimulationSeconds()};
    }

private:
    CaptureSessionClock m_clock = CaptureSessionClock::RealTime;
    unsigned int m_framesPerSecond = 60;
    std::uint64_t m_sceneUpdates = 0;
    std::uint64_t m_fixedUpdates = 0;
    double m_liveSimulationSeconds = 0.0;
};
} // namespace RtPbrSurvey
