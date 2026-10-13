#include "stdafx.h"

#include "Renderer/Mp4EncoderWorker.h"
#include "Runtime/CaptureSession.h"
#include "Runtime/CaptureSessionUi.h"

#include <chrono>
#include <iostream>
#include <stdexcept>

namespace
{
bool Check(bool condition, const char* message)
{
    if (!condition)
        std::cerr << "FAILED: " << message << '\n';
    return condition;
}

struct WriterState
{
    std::mutex mutex;
    std::condition_variable changed;
    bool holdAppend = true;
    bool holdFinalize = true;
    bool failAppend = false;
    bool failFinalize = false;
    bool throwAppend = false;
    bool throwFinalize = false;
    unsigned int appendEntries = 0;
    unsigned int finalizeEntries = 0;
    unsigned int resets = 0;
    bool sameThread = true;
    bool destroyed = false;
    std::thread::id thread;
    std::vector<std::uint64_t> order;
    std::optional<std::uint64_t> endTime;

    bool Wait(const std::function<bool()>& predicate)
    {
        std::unique_lock<std::mutex> lock(mutex);
        return changed.wait_for(lock, std::chrono::seconds(5), predicate);
    }
    void Release()
    {
        std::lock_guard<std::mutex> lock(mutex);
        holdAppend = false;
        holdFinalize = false;
        changed.notify_all();
    }
};

class TestWriter final : public Engine::Mp4FrameWriter
{
public:
    explicit TestWriter(std::shared_ptr<WriterState> state) : m_state(std::move(state))
    {
        std::lock_guard<std::mutex> lock(m_state->mutex);
        m_state->thread = std::this_thread::get_id();
    }
    ~TestWriter() override
    {
        std::lock_guard<std::mutex> lock(m_state->mutex);
        m_state->sameThread &= m_state->thread == std::this_thread::get_id();
        m_state->destroyed = true;
    }
    bool Append(const Engine::Mp4Frame& frame, std::string& error) override
    {
        std::unique_lock<std::mutex> lock(m_state->mutex);
        m_state->sameThread &= m_state->thread == std::this_thread::get_id();
        ++m_state->appendEntries;
        m_state->changed.notify_all();
        m_state->changed.wait(lock, [this]() { return !m_state->holdAppend; });
        m_state->order.push_back(frame.request.requestId);
        if (m_state->throwAppend)
            throw std::runtime_error("Injected append exception.");
        if (m_state->failAppend)
            error = "Injected append failure.";
        return !m_state->failAppend;
    }
    bool Finalize(std::optional<std::uint64_t> endTime, std::string& error) override
    {
        std::unique_lock<std::mutex> lock(m_state->mutex);
        m_state->sameThread &= m_state->thread == std::this_thread::get_id();
        ++m_state->finalizeEntries;
        m_state->endTime = endTime;
        m_state->changed.notify_all();
        m_state->changed.wait(lock, [this]() { return !m_state->holdFinalize; });
        if (m_state->throwFinalize)
        {
            throw std::runtime_error("Injected finalize exception.");
        }
        if (m_state->failFinalize)
            error = "Injected finalize failure.";
        return !m_state->failFinalize;
    }
    void Reset() override
    {
        std::lock_guard<std::mutex> lock(m_state->mutex);
        m_state->sameThread &= m_state->thread == std::this_thread::get_id();
        ++m_state->resets;
    }

private:
    std::shared_ptr<WriterState> m_state;
};

Engine::Mp4Frame Frame(std::uint64_t id)
{
    Engine::Mp4Frame frame;
    frame.request.path = "unused-test-video.mp4";
    frame.request.requestId = id;
    frame.width = 2;
    frame.height = 2;
    frame.pixels.assign(16, static_cast<std::uint8_t>(id));
    return frame;
}

bool WaitState(Engine::Mp4EncoderWorker& worker, Engine::Mp4WorkerState expected)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (worker.GetStatus().state != expected && std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return worker.GetStatus().state == expected;
}

bool TestOrderingAndFinalization()
{
    const auto state = std::make_shared<WriterState>();
    Engine::Mp4EncoderWorker worker([state]() { return std::make_unique<TestWriter>(state); }, 2, 32);
    std::string error;
    bool passed = Check(worker.BeginSession(error), "session begins without calling the writer");
    passed &= Check(worker.Enqueue(Frame(1), error), "first frame is accepted");
    passed &= Check(state->Wait([&]() { return state->appendEntries == 1; }), "writer is deliberately stalled");
    passed &= Check(state->thread != std::this_thread::get_id(), "writer runs on a dedicated thread");
    passed &= Check(worker.Enqueue(Frame(2), error), "enqueue remains responsive while append is blocked");
    passed &= Check(!worker.CanAcceptFrame(16), "full worker backpressures admission before rendering");
    passed &= Check(!worker.Enqueue(Frame(3), error), "capacity includes the executing frame");
    passed &= Check(worker.GetStatus().outstandingBytes == 32, "owned buffers respect byte budget");
    worker.RequestFinalize(1234567);
    worker.RequestFinalize(7654321);
    passed &= Check(worker.GetStatus().state == Engine::Mp4WorkerState::Finalizing && !worker.BeginSession(error) &&
                        !worker.Enqueue(Frame(3), error),
                    "finalization closes admission and blocks session restart");
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->holdAppend = false;
        state->changed.notify_all();
    }
    passed &= Check(state->Wait([&]() { return state->finalizeEntries == 1; }), "finalize follows all queued frames");
    passed &= Check(worker.GetStatus().state == Engine::Mp4WorkerState::Finalizing,
                    "completion is not published while finalize is blocked");
    const auto first = worker.ConsumeResult();
    const auto second = worker.ConsumeResult();
    passed &= Check(first && second && first->succeeded && second->succeeded && first->requestId == 1 &&
                        second->requestId == 2,
                    "results preserve frame identities and order");
    state->Release();
    passed &= Check(WaitState(worker, Engine::Mp4WorkerState::Completed), "finalization publishes completion");
    passed &= Check(state->order == std::vector<std::uint64_t>({1, 2}) && state->endTime == 1234567 &&
                        worker.GetStatus().processedFrames == 2 && worker.GetStatus().outstandingBytes == 0,
                    "order, initial end timestamp, and metrics are preserved");
    worker.RequestFinalize(999);
    passed &= Check(state->finalizeEntries == 1, "finalize is idempotent");
    passed &= Check(worker.BeginSession(error), "restart is allowed after completion and result consumption");
    worker.RequestFinalize(std::nullopt);
    passed &= Check(WaitState(worker, Engine::Mp4WorkerState::Completed), "empty session finalizes");
    worker.Shutdown();
    passed &= Check(state->sameThread && state->destroyed, "writer lifetime stays on the worker thread");
    return passed;
}

bool TestFailuresAndAbort()
{
    bool passed = true;
    for (int scenario = 0; scenario < 5; ++scenario)
    {
        const auto state = std::make_shared<WriterState>();
        state->failAppend = scenario == 0;
        state->throwAppend = scenario == 1;
        state->failFinalize = scenario == 2;
        state->throwFinalize = scenario == 4;
        Engine::Mp4EncoderWorker worker([state]() { return std::make_unique<TestWriter>(state); }, 2, 32);
        std::string error;
        passed &= Check(worker.BeginSession(error) && worker.Enqueue(Frame(1), error), "failure session starts");
        passed &= Check(state->Wait([&]() { return state->appendEntries == 1; }), "append reaches injected gate");
        passed &= Check(worker.Enqueue(Frame(2), error), "second frame queues before failure");
        if (scenario == 3)
            worker.RequestAbort();
        else
            worker.RequestFinalize(std::nullopt);
        state->Release();
        const auto terminal = scenario == 3 ? Engine::Mp4WorkerState::Aborted : Engine::Mp4WorkerState::Failed;
        passed &= Check(WaitState(worker, terminal), "failure or abort reaches terminal state");
        const auto first = worker.ConsumeResult();
        const auto second = worker.ConsumeResult();
        passed &= Check(first && second && first->requestId == 1 && second->requestId == 2,
                        "every accepted request gets a result even on failure or abort");
        if (scenario < 2 || scenario == 3)
            passed &= Check(first && second && !first->succeeded && !second->succeeded,
                            "append failure or abort also fails queued frames");
        passed &= Check(worker.GetStatus().outstandingBytes == 0, "failure releases the byte budget");
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            state->failAppend = false;
            state->throwAppend = false;
            state->failFinalize = false;
            state->throwFinalize = false;
        }
        passed &= Check(worker.BeginSession(error) && worker.Enqueue(Frame(3), error),
                        "a terminal failure can restart after consuming results");
        worker.RequestFinalize(std::nullopt);
        passed &= Check(WaitState(worker, Engine::Mp4WorkerState::Completed), "restarted session completes");
        const auto restarted = worker.ConsumeResult();
        passed &= Check(restarted && restarted->succeeded && restarted->requestId == 3,
                        "restart does not inherit failed requests");
        worker.Shutdown();
        passed &= Check(state->sameThread && state->destroyed, "failure cleanup preserves thread ownership");
    }
    return passed;
}

bool TestShutdownWithWork()
{
    const auto state = std::make_shared<WriterState>();
    Engine::Mp4EncoderWorker worker([state]() { return std::make_unique<TestWriter>(state); });
    std::string error;
    bool passed = Check(worker.BeginSession(error) && worker.Enqueue(Frame(1), error), "shutdown session starts");
    passed &= Check(state->Wait([&]() { return state->appendEntries == 1; }), "work is active at shutdown");
    passed &= Check(worker.Enqueue(Frame(2), error), "work is queued at shutdown");
    std::thread release([state]() { state->Release(); });
    worker.Shutdown();
    release.join();
    passed &= Check(state->destroyed && state->sameThread && worker.GetStatus().outstandingBytes == 0,
                    "shutdown joins the worker and releases all buffers");
    worker.Shutdown();
    return passed;
}

bool TestByteCapacity()
{
    const auto state = std::make_shared<WriterState>();
    Engine::Mp4EncoderWorker worker([state]() { return std::make_unique<TestWriter>(state); }, 3, 16);
    std::string error;
    bool passed = Check(worker.BeginSession(error) && worker.Enqueue(Frame(1), error), "byte-budget session starts");
    passed &= Check(state->Wait([&]() { return state->appendEntries == 1; }), "first buffer occupies byte budget");
    passed &= Check(!worker.Enqueue(Frame(2), error) && worker.GetStatus().outstandingFrames == 1,
                    "byte budget rejects a frame before the frame-count limit");
    state->Release();
    worker.RequestFinalize(std::nullopt);
    passed &= Check(WaitState(worker, Engine::Mp4WorkerState::Completed), "byte-budget session finalizes");
    worker.ConsumeResult();
    return passed;
}

bool TestSessionStateAndEndTime()
{
    RtPbrSurvey::CaptureSessionConfig config;
    config.outputDirectory = std::filesystem::temp_directory_path();
    config.baseName = "worker-state-test";
    config.outputFormat = RtPbrSurvey::CaptureSessionOutputFormat::Mp4;
    config.frameLimit = 1;
    RtPbrSurvey::CaptureSession session;
    std::string error;
    bool passed = Check(session.Start(config, error), "MP4 scheduler starts");
    session.Update({0, 10, 0});
    const auto request = session.AcquireReadyRequest();
    if (!Check(request.has_value(), "scheduler produces a frame"))
        return false;
    session.MarkRequestAccepted();
    session.Update({1, 10.25, 0.25});
    session.Stop();
    passed &= Check(session.GetStatus().state == RtPbrSurvey::CaptureSessionState::Draining,
                    "stop waits for the in-flight writer result");
    session.CompleteRequest({request->path, true, {}, 2, 2, request->requestId});
    passed &= Check(session.GetStatus().state == RtPbrSurvey::CaptureSessionState::Finalizing &&
                        RtPbrSurvey::CaptureSessionUi::IsActive(session.GetStatus()) && !session.Start(config, error),
                    "finalization remains active and prevents restart");
    session.Update({2, 99, 99});
    passed &= Check(session.GetVideoEndTimestamp100ns() == 2500000,
                    "drain and finalize latency do not extend the recording timeline");
    session.CompleteFinalization();
    passed &= Check(session.GetStatus().state == RtPbrSurvey::CaptureSessionState::Completed,
                    "worker acknowledgment completes the scheduler");
    passed &= Check(session.Start(config, error), "completed scheduler can restart");
    session.Stop();
    session.FailFinalization("Injected failure.");
    passed &=
        Check(session.GetStatus().state == RtPbrSurvey::CaptureSessionState::Failed, "finalize failure reaches Failed");
    return passed;
}
bool TestConcurrentSessionRequests()
{
    RtPbrSurvey::CaptureSessionConfig config;
    config.outputDirectory = std::filesystem::temp_directory_path();
    config.baseName = "pipeline-test";
    config.outputFormat = RtPbrSurvey::CaptureSessionOutputFormat::Mp4;
    config.clock = RtPbrSurvey::CaptureSessionClock::FixedStep;
    config.frameLimit = 4;
    RtPbrSurvey::CaptureSession session;
    std::string error;
    bool passed = Check(session.Start(config, error, 2), "two-frame MP4 pipeline starts");
    session.Update({0, 0, 0});
    const auto first = session.AcquireReadyRequest();
    session.MarkRequestAccepted();
    passed &= Check(session.CanAdvanceFixedStep(), "pending encode leaves capacity for the next pose");
    session.Update({1, 1.0 / 60, 1.0 / 60}, false);
    passed &= Check(!session.AcquireReadyRequest() && session.GetStatus().droppedFrameCount == 0,
                    "fixed-step pressure does not drop or generate a stale frame");
    session.Update({1, 1.0 / 60, 1.0 / 60});
    const auto second = session.AcquireReadyRequest();
    session.MarkRequestAccepted();
    if (!Check(first && second, "two distinct requests are generated"))
        return false;
    passed &= Check(!session.CanAdvanceFixedStep() && session.GetActiveRequestIds().size() == 2,
                    "full pipeline backpressures generation");
    session.Stop();
    session.CompleteRequest({second->path, true, {}, 2, 2, second->requestId});
    passed &= Check(session.IsActive() && session.GetStatus().state == RtPbrSurvey::CaptureSessionState::Draining,
                    "stop waits for every request even with out-of-order completions");
    session.CompleteRequest({second->path, true, {}, 2, 2, second->requestId});
    passed &= Check(session.GetStatus().savedFrameCount == 1, "duplicate completion does not increment saved count");
    session.CompleteRequest({first->path, true, {}, 2, 2, first->requestId});
    passed &= Check(session.GetStatus().state == RtPbrSurvey::CaptureSessionState::Finalizing,
                    "all completed requests trigger finalization");
    session.CompleteFinalization();
    passed &= Check(session.Start(config, error, 2), "concurrent session restarts");
    session.Update({0, 0, 0});
    const auto failed = session.AcquireReadyRequest();
    session.MarkRequestAccepted();
    session.Update({1, 1.0 / 60, 1.0 / 60});
    const auto draining = session.AcquireReadyRequest();
    session.MarkRequestAccepted();
    if (!Check(failed && draining, "failure pipeline has two accepted requests"))
        return false;
    session.CompleteRequest({failed->path, false, "first failure", 2, 2, failed->requestId});
    passed &= Check(session.GetStatus().state == RtPbrSurvey::CaptureSessionState::Draining && session.IsActive(),
                    "failure keeps ownership until remaining work drains");
    session.CompleteRequest({draining->path, false, "second failure", 2, 2, draining->requestId});
    passed &= Check(session.GetStatus().state == RtPbrSurvey::CaptureSessionState::Failed &&
                        session.GetStatus().error == "first failure",
                    "first error survives cleanup");
    config.clock = RtPbrSurvey::CaptureSessionClock::RealTime;
    passed &= Check(session.Start(config, error, 2), "real-time pipeline starts");
    session.Update({0, 10, 0}, false);
    session.Update({1, 10.05, 0.05}, false);
    passed &= Check(session.GetStatus().droppedFrameCount > 0 && !session.AcquireReadyRequest(),
                    "real-time pressure counts skipped intervals without blocking live time");
    session.Update({2, 10.1, 0.1});
    const auto realtime = session.AcquireReadyRequest();
    passed &= Check(realtime && realtime->videoTimestamp100ns == 1000000, "PTS preserve the skipped interval");
    return passed;
}
} // namespace

int main()
{
    return TestOrderingAndFinalization() && TestFailuresAndAbort() && TestShutdownWithWork() && TestByteCapacity() &&
                   TestSessionStateAndEndTime() && TestConcurrentSessionRequests()
               ? 0
               : 1;
}
