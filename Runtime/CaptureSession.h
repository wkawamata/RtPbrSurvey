#pragma once

#include "Shared/Screenshot.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace RtPbrSurvey
{
    enum class CaptureSessionOutputFormat
    {
        Png,
        Exr,
        Gif,
        Mp4,
    };

    enum class CaptureSessionClock
    {
        RealTime,
        FixedStep,
    };

    enum class CaptureSessionState
    {
        Idle,
        Warmup,
        Recording,
        Draining,
        Completed,
        Failed,
    };

    struct CaptureSessionConfig
    {
        std::filesystem::path outputDirectory;
        std::string baseName;
        CaptureSessionOutputFormat outputFormat = CaptureSessionOutputFormat::Png;
        ScreenshotCaptureSource source = ScreenshotCaptureSource::FinalOutput;
        std::optional<ScreenshotRegion> region;
        CaptureSessionClock clock = CaptureSessionClock::RealTime;
        std::uint32_t framesPerSecond = 60;
        std::uint32_t warmupFrames = 0;
        std::optional<std::uint64_t> frameLimit;
        std::optional<double> durationSeconds;

        // Preserves an exact legacy filename for a one-frame capture.
        std::optional<std::filesystem::path> singleOutputPath;
    };

    struct CaptureSessionTiming
    {
        std::uint64_t renderFrameIndex = 0;
        double realTimeSeconds = 0.0;
        double simulationTimeSeconds = 0.0;
    };

    struct CaptureSessionStatus
    {
        CaptureSessionState state = CaptureSessionState::Idle;
        std::uint64_t acceptedFrameCount = 0;
        std::uint64_t savedFrameCount = 0;
        std::uint64_t droppedFrameCount = 0;
        std::filesystem::path lastOutputPath;
        std::string error;
    };

    // Host-driven capture scheduler. It never waits for the GPU. A host acquires a
    // request, submits it to its renderer, then reports the asynchronous result.
    class CaptureSession
    {
    public:
        bool Start(const CaptureSessionConfig& config, std::string& error);
        void Stop();
        void Update(const CaptureSessionTiming& timing);
        std::optional<ScreenshotRequest> AcquireReadyRequest();
        void MarkRequestAccepted();
        void CompleteRequest(ScreenshotResult result);

        bool CanAdvanceFixedStep() const;
        bool IsActive() const;
        std::optional<std::uint64_t> GetActiveRequestId() const;
        const CaptureSessionStatus& GetStatus() const;

    private:
        bool IsRecordingLimitReached(double clockSeconds) const;
        double GetClockSeconds(const CaptureSessionTiming& timing) const;
        void BeginDraining();
        void FinishDraining();
        std::filesystem::path BuildOutputPath() const;

        CaptureSessionConfig m_config;
        CaptureSessionStatus m_status;
        std::optional<CaptureSessionTiming> m_startTiming;
        std::optional<CaptureSessionTiming> m_recordingStartTiming;
        std::optional<ScreenshotRequest> m_readyRequest;
        std::optional<std::uint64_t> m_activeRequestId;
        bool m_requestInFlight = false;
        bool m_stopRequested = false;
        double m_nextCaptureSeconds = 0.0;
        std::uint64_t m_nextRequestId = 1;
    };
} // namespace RtPbrSurvey
