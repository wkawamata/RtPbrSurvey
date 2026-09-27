#include "stdafx.h"

#include "Runtime/CaptureSession.h"

#include <cmath>
#include <iomanip>
#include <sstream>

namespace RtPbrSurvey
{
    namespace
    {
        const char* GetExtension(CaptureSessionOutputFormat format)
        {
            switch (format)
            {
                case CaptureSessionOutputFormat::Png:
                    return ".png";
                case CaptureSessionOutputFormat::Exr:
                    return ".exr";
                default:
                    return "";
            }
        }

        ScreenshotOutputFormat GetScreenshotOutputFormat(CaptureSessionOutputFormat format)
        {
            return format == CaptureSessionOutputFormat::Exr ? ScreenshotOutputFormat::Exr : ScreenshotOutputFormat::Png;
        }
    } // namespace

    bool CaptureSession::Start(const CaptureSessionConfig& config, std::string& error)
    {
        error.clear();
        if (IsActive())
        {
            error = "A capture session is already active.";
            return false;
        }
        if (config.outputFormat == CaptureSessionOutputFormat::Gif || config.outputFormat == CaptureSessionOutputFormat::Mp4)
        {
            error = "GIF and MP4 capture sessions are not implemented.";
            return false;
        }
        if (config.outputDirectory.empty() || config.baseName.empty())
        {
            error = "Capture session requires an output directory and base name.";
            return false;
        }
        if (config.framesPerSecond == 0)
        {
            error = "Capture session framesPerSecond must be greater than zero.";
            return false;
        }
        if (!config.frameLimit.has_value() && !config.durationSeconds.has_value())
        {
            error = "Capture session requires a frame limit or duration.";
            return false;
        }
        if (config.frameLimit.has_value() && *config.frameLimit == 0)
        {
            error = "Capture session frame limit must be greater than zero.";
            return false;
        }
        if (config.durationSeconds.has_value() && (!std::isfinite(*config.durationSeconds) || *config.durationSeconds <= 0.0))
        {
            error = "Capture session duration must be finite and greater than zero.";
            return false;
        }
        if (config.region.has_value() && (config.region->width == 0 || config.region->height == 0))
        {
            error = "Capture session region dimensions must be greater than zero.";
            return false;
        }
        if (config.singleOutputPath.has_value() && config.frameLimit != std::optional<std::uint64_t>(1))
        {
            error = "A single output path requires a frame limit of one.";
            return false;
        }
        if (config.outputFormat == CaptureSessionOutputFormat::Png && config.source != ScreenshotCaptureSource::FinalOutput)
        {
            error = "PNG capture sessions require the final output source.";
            return false;
        }
        if (config.outputFormat == CaptureSessionOutputFormat::Exr && config.source != ScreenshotCaptureSource::PreToneMapSceneColor)
        {
            error = "EXR capture sessions require the pre-tone-map scene color source.";
            return false;
        }

        CaptureSessionConfig resolvedConfig = config;
        std::error_code pathError;
        resolvedConfig.outputDirectory = std::filesystem::absolute(config.outputDirectory, pathError).lexically_normal();
        if (!pathError && config.singleOutputPath.has_value())
        {
            resolvedConfig.singleOutputPath = std::filesystem::absolute(*config.singleOutputPath, pathError).lexically_normal();
        }
        if (pathError)
        {
            error = "Unable to resolve capture output path: " + pathError.message();
            return false;
        }

        m_config = std::move(resolvedConfig);
        m_status = {};
        m_startTiming.reset();
        m_recordingStartTiming.reset();
        m_readyRequest.reset();
        m_activeRequestId.reset();
        m_requestInFlight = false;
        m_stopRequested = false;
        m_nextCaptureSeconds = 0.0;
        m_status.state = config.warmupFrames > 0 ? CaptureSessionState::Warmup : CaptureSessionState::Recording;
        return true;
    }

    void CaptureSession::Stop()
    {
        if (m_status.state == CaptureSessionState::Warmup || m_status.state == CaptureSessionState::Recording)
        {
            m_stopRequested = true;
            m_readyRequest.reset();
            BeginDraining();
        }
    }

    void CaptureSession::Update(const CaptureSessionTiming& timing)
    {
        if (m_status.state != CaptureSessionState::Warmup && m_status.state != CaptureSessionState::Recording)
        {
            return;
        }

        if (!m_startTiming.has_value())
        {
            m_startTiming = timing;
            m_nextCaptureSeconds = GetClockSeconds(timing);
            if (m_status.state == CaptureSessionState::Recording)
            {
                m_recordingStartTiming = timing;
            }
        }

        if (m_status.state == CaptureSessionState::Warmup)
        {
            if (timing.renderFrameIndex - m_startTiming->renderFrameIndex < m_config.warmupFrames)
            {
                return;
            }
            m_status.state = CaptureSessionState::Recording;
            m_recordingStartTiming = timing;
            m_nextCaptureSeconds = GetClockSeconds(timing);
        }

        const double clockSeconds = GetClockSeconds(timing);
        if (IsRecordingLimitReached(clockSeconds))
        {
            BeginDraining();
            return;
        }

        if (m_readyRequest.has_value())
        {
            return;
        }

        const double interval = 1.0 / static_cast<double>(m_config.framesPerSecond);
        if (m_requestInFlight)
        {
            if (m_config.clock == CaptureSessionClock::RealTime)
            {
                while (clockSeconds >= m_nextCaptureSeconds)
                {
                    ++m_status.droppedFrameCount;
                    m_nextCaptureSeconds += interval;
                }
            }
            return;
        }

        if (clockSeconds < m_nextCaptureSeconds)
        {
            return;
        }

        ScreenshotRequest request = {
            BuildOutputPath(),
            m_config.region,
            GetScreenshotOutputFormat(m_config.outputFormat),
            m_config.source,
        };
        request.requestId = m_nextRequestId++;
        m_readyRequest = std::move(request);
    }

    std::optional<ScreenshotRequest> CaptureSession::AcquireReadyRequest()
    {
        if (!m_readyRequest.has_value())
        {
            return std::nullopt;
        }
        return m_readyRequest;
    }

    void CaptureSession::MarkRequestAccepted()
    {
        if (!m_readyRequest.has_value())
        {
            return;
        }

        m_status.lastOutputPath = m_readyRequest->path;
        ++m_status.acceptedFrameCount;
        m_activeRequestId = m_readyRequest->requestId;
        m_readyRequest.reset();
        m_requestInFlight = true;
        m_nextCaptureSeconds += 1.0 / static_cast<double>(m_config.framesPerSecond);
    }

    void CaptureSession::CompleteRequest(ScreenshotResult result)
    {
        if (!m_requestInFlight || !m_activeRequestId.has_value() || result.requestId != *m_activeRequestId)
        {
            return;
        }

        m_requestInFlight = false;
        m_activeRequestId.reset();
        // Keep the accepted absolute request path stable across renderer completion.
        if (!result.succeeded)
        {
            m_status.error = std::move(result.error);
            BeginDraining();
            m_status.state = CaptureSessionState::Failed;
            return;
        }

        ++m_status.savedFrameCount;
        if (m_status.state == CaptureSessionState::Draining || m_stopRequested ||
            IsRecordingLimitReached(m_nextCaptureSeconds))
        {
            BeginDraining();
        }
    }

    bool CaptureSession::CanAdvanceFixedStep() const
    {
        return m_config.clock != CaptureSessionClock::FixedStep ||
               (!m_readyRequest.has_value() && !m_requestInFlight);
    }

    bool CaptureSession::IsActive() const
    {
        return m_status.state == CaptureSessionState::Warmup || m_status.state == CaptureSessionState::Recording ||
               m_status.state == CaptureSessionState::Draining;
    }

    std::optional<std::uint64_t> CaptureSession::GetActiveRequestId() const
    {
        return m_activeRequestId;
    }

    const CaptureSessionStatus& CaptureSession::GetStatus() const
    {
        return m_status;
    }

    bool CaptureSession::IsRecordingLimitReached(double clockSeconds) const
    {
        if (m_config.frameLimit.has_value() && m_status.acceptedFrameCount >= *m_config.frameLimit)
        {
            return true;
        }
        return m_config.durationSeconds.has_value() && m_recordingStartTiming.has_value() &&
               clockSeconds - GetClockSeconds(*m_recordingStartTiming) >= *m_config.durationSeconds;
    }

    double CaptureSession::GetClockSeconds(const CaptureSessionTiming& timing) const
    {
        return m_config.clock == CaptureSessionClock::FixedStep ? timing.simulationTimeSeconds : timing.realTimeSeconds;
    }

    void CaptureSession::BeginDraining()
    {
        m_readyRequest.reset();
        m_status.state = CaptureSessionState::Draining;
        if (!m_requestInFlight)
        {
            FinishDraining();
        }
    }

    void CaptureSession::FinishDraining()
    {
        if (m_status.state == CaptureSessionState::Draining)
        {
            m_status.state = m_status.error.empty() ? CaptureSessionState::Completed : CaptureSessionState::Failed;
        }
    }

    std::filesystem::path CaptureSession::BuildOutputPath() const
    {
        if (m_config.singleOutputPath.has_value())
        {
            return *m_config.singleOutputPath;
        }

        std::ostringstream name;
        name << m_config.baseName << '_' << std::setw(6) << std::setfill('0') << m_status.acceptedFrameCount
             << GetExtension(m_config.outputFormat);
        return m_config.outputDirectory / name.str();
    }
} // namespace RtPbrSurvey
