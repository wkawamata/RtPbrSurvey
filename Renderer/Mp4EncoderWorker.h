#pragma once

#include "Shared/Screenshot.h"

#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace Engine
{
struct Mp4Frame
{
    RtPbrSurvey::ScreenshotRequest request;
    unsigned int width = 0;
    unsigned int height = 0;
    std::vector<std::uint8_t> pixels;
};

// Constructed, called, and destroyed exclusively on the encoder thread.
class Mp4FrameWriter
{
public:
    virtual ~Mp4FrameWriter() = default;
    virtual bool Append(const Mp4Frame& frame, std::string& error) = 0;
    virtual bool Finalize(std::optional<std::uint64_t> endTime, std::string& error) = 0;
    virtual void Reset() = 0;
};

enum class Mp4WorkerState
{
    Idle,
    Recording,
    Finalizing,
    Completed,
    Failed,
    Aborted,
};

struct Mp4WorkerStatus
{
    Mp4WorkerState state = Mp4WorkerState::Idle;
    std::size_t outstandingFrames = 0;
    std::size_t outstandingBytes = 0;
    std::uint64_t processedFrames = 0;
    double lastAppendMilliseconds = 0.0;
    double maxAppendMilliseconds = 0.0;
    double finalizeMilliseconds = 0.0;
    std::string error;
};

class Mp4EncoderWorker
{
public:
    using WriterFactory = std::function<std::unique_ptr<Mp4FrameWriter>()>;
    explicit Mp4EncoderWorker(WriterFactory factory = {},
                              std::size_t maxFrames = 2,
                              std::size_t maxBytes = 128 * 1024 * 1024);
    ~Mp4EncoderWorker();
    Mp4EncoderWorker(const Mp4EncoderWorker&) = delete;
    Mp4EncoderWorker& operator=(const Mp4EncoderWorker&) = delete;

    bool BeginSession(std::string& error);
    bool CanFitFrame(std::size_t bytes) const
    {
        return bytes > 0 && bytes <= m_maxBytes;
    }
    bool CanAcceptFrame(std::size_t bytes) const;
    bool Enqueue(Mp4Frame&& frame, std::string& error);
    std::optional<RtPbrSurvey::ScreenshotResult> ConsumeResult();
    void RequestFinalize(std::optional<std::uint64_t> endTime);
    void RequestAbort();
    Mp4WorkerStatus GetStatus() const;
    void Shutdown();

private:
    void Run();
    void FailQueuedFrames(const std::string& error);
    WriterFactory m_factory;
    const std::size_t m_maxFrames;
    const std::size_t m_maxBytes;
    mutable std::mutex m_mutex;
    std::condition_variable m_wake;
    std::deque<Mp4Frame> m_frames;
    std::deque<RtPbrSurvey::ScreenshotResult> m_results;
    Mp4WorkerStatus m_status;
    std::optional<std::uint64_t> m_endTime;
    bool m_finalizeRequested = false;
    bool m_abortRequested = false;
    bool m_shutdown = false;
    std::thread m_thread;
};
} // namespace Engine
