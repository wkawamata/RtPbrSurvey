#pragma once

#include <DirectXMath.h>
#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace RtPbrSurvey
{
struct CaptureOrbitPose
{
    double timeSeconds = 0.0;
    float yaw = 0.0f;
    float pitch = 0.0f;
    float distance = 1.0f;
    DirectX::XMFLOAT3 pivot = {};
};

// Immutable baseline; evaluation never depends on previous evaluations or render cadence.
class CaptureOrbitTrack
{
public:
    CaptureOrbitTrack(CaptureOrbitPose baseline, double degreesPerSecond, unsigned int framesPerSecond)
        : m_baseline(baseline), m_degreesPerSecond(degreesPerSecond), m_framesPerSecond(framesPerSecond)
    {
        if (framesPerSecond == 0 || !std::isfinite(degreesPerSecond) || !std::isfinite(baseline.yaw) ||
            !std::isfinite(baseline.pitch) || !std::isfinite(baseline.distance) || baseline.distance <= 0 ||
            !std::isfinite(baseline.pivot.x) || !std::isfinite(baseline.pivot.y) || !std::isfinite(baseline.pivot.z))
            throw std::invalid_argument("Capture orbit requires a finite baseline/rate, positive distance and FPS.");
    }

    CaptureOrbitPose EvaluateFrame(std::uint64_t frameIndex) const
    {
        CaptureOrbitPose pose = m_baseline;
        pose.timeSeconds = static_cast<double>(frameIndex) / m_framesPerSecond;
        constexpr double pi = 3.14159265358979323846;
        const double yaw = m_baseline.yaw + m_degreesPerSecond * pose.timeSeconds * pi / 180.0;
        pose.yaw = static_cast<float>(std::remainder(yaw, 2.0 * pi));
        return pose;
    }

private:
    CaptureOrbitPose m_baseline;
    double m_degreesPerSecond;
    unsigned int m_framesPerSecond;
};
} // namespace RtPbrSurvey
