#include "stdafx.h"

#include "Renderer/Mp4EncoderWorker.h"
#include "Renderer/Mp4Encoder.h"

#include <chrono>
#include <algorithm>
#include <stdexcept>

namespace Engine
{
namespace
{
class MediaFoundationWriter final : public Mp4FrameWriter
{
public:
    bool Append(const Mp4Frame& frame, std::string& error) override
    {
        return m_encoder.AppendFrame(frame.request.path,
                                     frame.width,
                                     frame.height,
                                     frame.pixels.data(),
                                     frame.request.videoFramesPerSecond,
                                     frame.request.videoBitrate,
                                     error,
                                     frame.request.videoTimestamp100ns);
    }
    bool Finalize(std::optional<std::uint64_t> endTime, std::string& error) override
    {
        return m_encoder.Finalize(error, endTime);
    }
    void Reset() override
    {
        m_encoder.Reset();
    }

private:
    Mp4Encoder m_encoder;
};

double ElapsedMilliseconds(std::chrono::steady_clock::time_point start)
{
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}
} // namespace

Mp4EncoderWorker::Mp4EncoderWorker(WriterFactory factory, std::size_t maxFrames, std::size_t maxBytes)
    : m_factory(factory ? std::move(factory) : []() { return std::make_unique<MediaFoundationWriter>(); }),
      m_maxFrames(maxFrames), m_maxBytes(maxBytes)
{
}

Mp4EncoderWorker::~Mp4EncoderWorker()
{
    Shutdown();
}

bool Mp4EncoderWorker::BeginSession(std::string& error)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    error.clear();
    if (m_shutdown || m_abortRequested || m_status.outstandingFrames != 0 || !m_results.empty() ||
        m_status.state == Mp4WorkerState::Recording || m_status.state == Mp4WorkerState::Finalizing)
    {
        error = "MP4 encoder is still processing the previous session.";
        return false;
    }
    if (!m_thread.joinable())
    {
        try
        {
            m_thread = std::thread(&Mp4EncoderWorker::Run, this);
        }
        catch (const std::exception& exception)
        {
            error = exception.what();
            return false;
        }
    }
    m_status = {};
    m_status.state = Mp4WorkerState::Recording;
    m_finalizeRequested = false;
    m_endTime.reset();
    return true;
}

bool Mp4EncoderWorker::CanAcceptFrame(std::size_t bytes) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return !m_shutdown && !m_abortRequested && m_status.state == Mp4WorkerState::Recording &&
           m_status.outstandingFrames + m_results.size() < m_maxFrames && CanFitFrame(bytes) &&
           m_status.outstandingBytes <= m_maxBytes - bytes;
}

bool Mp4EncoderWorker::SetDiagnosticAppendDelay(unsigned int milliseconds)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (milliseconds > 1000 || m_shutdown || m_abortRequested || m_status.outstandingFrames != 0 ||
        !m_results.empty() || m_status.state == Mp4WorkerState::Recording ||
        m_status.state == Mp4WorkerState::Finalizing)
        return false;
    m_diagnosticAppendDelayMilliseconds = milliseconds;
    return true;
}

bool Mp4EncoderWorker::Enqueue(Mp4Frame&& frame, std::string& error)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    error.clear();
    const std::uint64_t required = static_cast<std::uint64_t>(frame.width) * frame.height * 4;
    if (frame.width == 0 || frame.height == 0 || frame.width > 16384 || frame.height > 16384 ||
        required != frame.pixels.size())
    {
        error = "MP4 worker requires an owned, tightly packed RGBA8 frame.";
        return false;
    }
    const auto reservation =
        std::find_if(m_reservations.begin(),
                     m_reservations.end(),
                     [&frame](const Reservation& value) { return value.requestId == frame.request.requestId; });
    const bool reserved = reservation != m_reservations.end();
    if (reserved && reservation->bytes != required)
    {
        error = "MP4 frame does not match its capacity reservation.";
        return false;
    }
    if (m_shutdown || m_abortRequested ||
        (m_status.state != Mp4WorkerState::Recording && !(reserved && m_status.state == Mp4WorkerState::Finalizing)))
    {
        error = "MP4 encoder is not accepting frames.";
        return false;
    }
    if (!reserved && (m_status.outstandingFrames + m_results.size() >= m_maxFrames ||
                      frame.pixels.size() > m_maxBytes || m_status.outstandingBytes > m_maxBytes - frame.pixels.size()))
    {
        error = "MP4 encoder queue capacity exceeded.";
        return false;
    }
    const std::size_t bytes = frame.pixels.size();
    m_frames.push_back(std::move(frame));
    if (reserved)
        m_reservations.erase(reservation);
    else
    {
        ++m_status.outstandingFrames;
        m_status.outstandingBytes += bytes;
    }
    m_wake.notify_one();
    return true;
}

bool Mp4EncoderWorker::ReserveFrame(std::uint64_t requestId, std::size_t bytes, std::string& error)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    error.clear();
    if (m_shutdown || m_abortRequested || m_status.state != Mp4WorkerState::Recording ||
        m_status.outstandingFrames + m_results.size() >= m_maxFrames || !CanFitFrame(bytes) ||
        m_status.outstandingBytes > m_maxBytes - bytes)
    {
        error = "MP4 encoder has no capacity for a GPU capture reservation.";
        return false;
    }
    if (std::any_of(m_reservations.begin(),
                    m_reservations.end(),
                    [requestId](const Reservation& value) { return value.requestId == requestId; }))
    {
        error = "MP4 capture request is already reserved.";
        return false;
    }
    m_reservations.push_back({requestId, bytes});
    ++m_status.outstandingFrames;
    m_status.outstandingBytes += bytes;
    return true;
}

void Mp4EncoderWorker::CancelReservation(std::uint64_t requestId)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    const auto reservation =
        std::find_if(m_reservations.begin(),
                     m_reservations.end(),
                     [requestId](const Reservation& value) { return value.requestId == requestId; });
    if (reservation == m_reservations.end())
        return;
    --m_status.outstandingFrames;
    m_status.outstandingBytes -= reservation->bytes;
    m_reservations.erase(reservation);
    m_wake.notify_one();
}

void Mp4EncoderWorker::ReleaseReservations()
{
    for (const Reservation& reservation : m_reservations)
    {
        --m_status.outstandingFrames;
        m_status.outstandingBytes -= reservation.bytes;
    }
    m_reservations.clear();
}

std::optional<RtPbrSurvey::ScreenshotResult> Mp4EncoderWorker::ConsumeResult()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_results.empty())
        return std::nullopt;
    RtPbrSurvey::ScreenshotResult result = std::move(m_results.front());
    m_results.pop_front();
    return result;
}

void Mp4EncoderWorker::RequestFinalize(std::optional<std::uint64_t> endTime)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_status.state != Mp4WorkerState::Recording || m_shutdown || m_abortRequested)
        return;
    m_endTime = endTime;
    m_finalizeRequested = true;
    m_status.state = Mp4WorkerState::Finalizing;
    m_wake.notify_one();
}

void Mp4EncoderWorker::RequestAbort()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_shutdown || m_status.state == Mp4WorkerState::Idle || m_status.state == Mp4WorkerState::Aborted ||
        m_status.state == Mp4WorkerState::Failed || m_status.state == Mp4WorkerState::Completed)
        return;
    m_abortRequested = true;
    m_wake.notify_one();
}

Mp4WorkerStatus Mp4EncoderWorker::GetStatus() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_status;
}

void Mp4EncoderWorker::Shutdown()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_shutdown = true;
        m_wake.notify_one();
    }
    if (m_thread.joinable())
        m_thread.join();
}

void Mp4EncoderWorker::FailQueuedFrames(const std::string& error)
{
    // Called with the mutex held; the currently executing frame is accounted separately.
    for (const Mp4Frame& frame : m_frames)
    {
        m_results.push_back({frame.request.path, false, error, frame.width, frame.height, frame.request.requestId});
        --m_status.outstandingFrames;
        m_status.outstandingBytes -= frame.pixels.size();
    }
    m_frames.clear();
}

void Mp4EncoderWorker::Run()
{
    std::unique_ptr<Mp4FrameWriter> writer;
    for (;;)
    {
        Mp4Frame frame;
        bool finalize = false;
        std::optional<std::uint64_t> endTime;
        unsigned int appendDelay = 0;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_wake.wait(lock,
                        [this]()
                        {
                            return m_shutdown || m_abortRequested || !m_frames.empty() ||
                                   (m_finalizeRequested && m_reservations.empty());
                        });
            if (m_shutdown || m_abortRequested)
            {
                FailQueuedFrames("MP4 encoding aborted.");
                ReleaseReservations();
                m_finalizeRequested = false;
                lock.unlock();
                if (writer)
                    writer->Reset();
                writer.reset();
                lock.lock();
                m_abortRequested = false;
                m_status.state = Mp4WorkerState::Aborted;
                if (m_shutdown)
                    return;
                continue;
            }
            if (!m_frames.empty())
            {
                frame = std::move(m_frames.front());
                m_frames.pop_front();
                appendDelay = m_diagnosticAppendDelayMilliseconds;
            }
            else
            {
                finalize = true;
                endTime = m_endTime;
                m_finalizeRequested = false;
            }
        }

        std::string error;
        bool succeeded = false;
        const auto start = std::chrono::steady_clock::now();
        try
        {
            if (appendDelay > 0)
            {
                std::unique_lock<std::mutex> lock(m_mutex);
                if (m_wake.wait_for(lock,
                                    std::chrono::milliseconds(appendDelay),
                                    [this]() { return m_shutdown || m_abortRequested; }))
                    throw std::runtime_error("MP4 encoding aborted.");
            }
            if (!writer)
                writer = m_factory();
            if (!writer)
                throw std::runtime_error("MP4 writer factory returned no writer.");
            succeeded = finalize ? writer->Finalize(endTime, error) : writer->Append(frame, error);
            if (!succeeded)
                writer->Reset();
        }
        catch (const std::exception& exception)
        {
            error = exception.what();
            writer.reset();
        }
        catch (...)
        {
            error = "Unknown MP4 worker failure.";
            writer.reset();
        }
        const double elapsed = ElapsedMilliseconds(start);
        const std::size_t frameBytes = frame.pixels.size();
        std::vector<std::uint8_t>().swap(frame.pixels);
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_abortRequested || m_shutdown)
        {
            succeeded = false;
            error = "MP4 encoding aborted.";
        }
        if (finalize)
        {
            m_status.finalizeMilliseconds = elapsed;
            m_status.state = succeeded ? Mp4WorkerState::Completed : Mp4WorkerState::Failed;
        }
        else
        {
            --m_status.outstandingFrames;
            m_status.outstandingBytes -= frameBytes;
            m_status.lastAppendMilliseconds = elapsed;
            m_status.maxAppendMilliseconds = (std::max)(m_status.maxAppendMilliseconds, elapsed);
            if (succeeded)
                ++m_status.processedFrames;
            m_results.push_back(
                {frame.request.path, succeeded, error, frame.width, frame.height, frame.request.requestId});
            if (!succeeded)
            {
                FailQueuedFrames(error);
                ReleaseReservations();
                m_finalizeRequested = false;
                m_status.state = Mp4WorkerState::Failed;
            }
        }
        if (!succeeded)
            m_status.error = error;
    }
}
} // namespace Engine
