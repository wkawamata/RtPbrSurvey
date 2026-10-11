#include "stdafx.h"

#include "Runtime/CaptureSession.h"

#include <cmath>
#include <limits>
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
                case CaptureSessionOutputFormat::Gif:
                    return ".gif";
                case CaptureSessionOutputFormat::Mp4:
                    return ".mp4";
                default:
                    return "";
            }
        }

        ScreenshotOutputFormat GetScreenshotOutputFormat(CaptureSessionOutputFormat format)
        {
            switch (format)
            {
                case CaptureSessionOutputFormat::Exr:
                    return ScreenshotOutputFormat::Exr;
                case CaptureSessionOutputFormat::Gif:
                    return ScreenshotOutputFormat::Gif;
                case CaptureSessionOutputFormat::Mp4:
                    return ScreenshotOutputFormat::Mp4;
                default:
                    return ScreenshotOutputFormat::Png;
            }
        }

        bool IsRelativeSubdirectory(const std::filesystem::path& path)
        {
            if (path.empty())
            {
                return true;
            }
            if (path.has_root_name() || path.has_root_directory())
            {
                return false;
            }
            for (const std::filesystem::path& component : path)
            {
                if (component == "..")
                {
                    return false;
                }
            }
            return true;
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
        if (config.outputFormat == CaptureSessionOutputFormat::Mp4 &&
            (config.framesPerSecond > 240 || config.mp4Bitrate < 1000000 || config.mp4Bitrate > 100000000))
        {
            error = "MP4 requires FPS 1-240 and bitrate 1-100 Mbps.";
            return false;
        }
        if (config.outputDirectory.empty() || config.baseName.empty())
        {
            error = "Capture session requires an output directory and base name.";
            return false;
        }
        if (!IsRelativeSubdirectory(config.outputSubdirectory))
        {
            error = "Capture session subfolder must be a relative path below the output directory.";
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
        if (config.outputFormat == CaptureSessionOutputFormat::Gif && config.source != ScreenshotCaptureSource::FinalOutput)
        {
            error = "GIF capture sessions require the final output source.";
            return false;
        }
        if (config.outputFormat == CaptureSessionOutputFormat::Mp4 && config.source != ScreenshotCaptureSource::FinalOutput)
        {
            error = "MP4 capture sessions require the final output source.";
            return false;
        }
        if (config.gifRepeatMode != CaptureSessionGifRepeatMode::None &&
            config.gifRepeatMode != CaptureSessionGifRepeatMode::Infinite &&
            config.gifRepeatMode != CaptureSessionGifRepeatMode::Count)
        {
            error = "Capture session GIF repeat mode is invalid.";
            return false;
        }
        if (config.gifDisposal != CaptureSessionGifDisposal::Keep &&
            config.gifDisposal != CaptureSessionGifDisposal::Background &&
            config.gifDisposal != CaptureSessionGifDisposal::Previous)
        {
            error = "Capture session GIF disposal mode is invalid.";
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
        if (!pathError)
        {
            resolvedConfig.outputDirectory = (resolvedConfig.outputDirectory / config.outputSubdirectory).lexically_normal();
        }
        if (!pathError && config.singleOutputPath.has_value())
        {
            resolvedConfig.singleOutputPath = std::filesystem::absolute(*config.singleOutputPath, pathError).lexically_normal();
        }
        if (pathError)
        {
            error = "Unable to resolve capture output path: " + pathError.message();
            return false;
        }

        if ((resolvedConfig.outputFormat == CaptureSessionOutputFormat::Gif ||
             resolvedConfig.outputFormat == CaptureSessionOutputFormat::Mp4) && !resolvedConfig.singleOutputPath.has_value())
        {
            if (!ResolveVideoOutputPath(resolvedConfig, error))
            {
                return false;
            }
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
        m_lastClockSeconds = 0.0;
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
        m_lastClockSeconds = GetClockSeconds(timing);
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
        if (m_config.outputFormat == CaptureSessionOutputFormat::Gif)
        {
            request.frameDelayCentiseconds = static_cast<std::uint16_t>(
                (std::max)(1u, static_cast<unsigned int>(std::lround(100.0 / m_config.framesPerSecond))));
            if (m_config.gifRepeatMode == CaptureSessionGifRepeatMode::Infinite)
            {
                request.gifRepeatCount = 0;
            }
            else if (m_config.gifRepeatMode == CaptureSessionGifRepeatMode::Count)
            {
                request.gifRepeatCount = m_config.gifRepeatCount;
            }
            request.gifDisposal = static_cast<std::uint8_t>(m_config.gifDisposal);
        }
        request.requestId = m_nextRequestId++;
        request.videoFramesPerSecond = m_config.framesPerSecond;
        request.videoBitrate = m_config.mp4Bitrate;
        if (UsesMp4() && m_config.clock == CaptureSessionClock::RealTime)
        {
            request.videoTimestamp100ns = static_cast<std::uint64_t>(std::llround(
                (std::max)(0.0, clockSeconds - GetClockSeconds(*m_recordingStartTiming)) * 10000000.0));
        }
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

    bool CaptureSession::UsesAnimatedGif() const
    {
        return m_config.outputFormat == CaptureSessionOutputFormat::Gif;
    }

    bool CaptureSession::UsesMp4() const
    {
        return m_config.outputFormat == CaptureSessionOutputFormat::Mp4;
    }

    std::optional<std::uint64_t> CaptureSession::GetVideoEndTimestamp100ns() const
    {
        if (!UsesMp4() || m_config.clock != CaptureSessionClock::RealTime || !m_recordingStartTiming)
        {
            return std::nullopt;
        }
        double elapsed = (std::max)(0.0, m_lastClockSeconds - GetClockSeconds(*m_recordingStartTiming));
        if (m_config.durationSeconds) elapsed = (std::min)(elapsed, *m_config.durationSeconds);
        return static_cast<std::uint64_t>(std::llround(elapsed * 10000000.0));
    }

    void CaptureSession::FailFinalization(const std::string& error)
    {
        if (m_status.state == CaptureSessionState::Completed)
        {
            m_status.state = CaptureSessionState::Failed;
            m_status.error = error;
        }
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

    bool CaptureSession::ResolveVideoOutputPath(CaptureSessionConfig& config, std::string& error)
    {
        std::error_code fileError;
        const std::filesystem::path basePath = config.outputDirectory / (config.baseName + GetExtension(config.outputFormat));
        if (!std::filesystem::exists(basePath, fileError))
        {
            if (fileError)
            {
                error = "Unable to check video output path: " + fileError.message();
                return false;
            }
            config.singleOutputPath = basePath;
            return true;
        }

        for (std::uint64_t suffix = 1; suffix <= static_cast<std::uint64_t>((std::numeric_limits<unsigned int>::max)()); ++suffix)
        {
            std::ostringstream name;
            name << config.baseName << '_' << std::setw(6) << std::setfill('0') << suffix << GetExtension(config.outputFormat);
            const std::filesystem::path candidate = config.outputDirectory / name.str();
            fileError.clear();
            if (!std::filesystem::exists(candidate, fileError))
            {
                if (fileError)
                {
                    error = "Unable to check video output path: " + fileError.message();
                    return false;
                }
                config.singleOutputPath = candidate;
                return true;
            }
        }

        error = "Unable to find an unused video output filename.";
        return false;
    }

    std::filesystem::path CaptureSession::BuildOutputPath() const
    {
        if (m_config.singleOutputPath.has_value())
        {
            return *m_config.singleOutputPath;
        }

        std::ostringstream name;
        name << m_config.baseName;
        if (m_config.outputFormat != CaptureSessionOutputFormat::Gif && m_config.outputFormat != CaptureSessionOutputFormat::Mp4)
        {
            name << '_' << std::setw(6) << std::setfill('0') << m_status.acceptedFrameCount;
        }
        name << GetExtension(m_config.outputFormat);
        return m_config.outputDirectory / name.str();
    }
} // namespace RtPbrSurvey
