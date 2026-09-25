#include "stdafx.h"

#include "Renderer/ScreenshotRequestQueue.h"

#include <iostream>

namespace
{
bool Check(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "FAILED: " << message << '\n';
    }
    return condition;
}

bool TestPendingCaptureQueuesRequestsInFifoOrder()
{
    Engine::ScreenshotRequestQueue queue;
    queue.Enqueue({"first.png"});

    RtPbrSurvey::ScreenshotRequest activeRequest;
    bool passed = Check(queue.BeginNextCapture(activeRequest), "first request begins capture");
    passed &= Check(activeRequest.path == "first.png", "first request is selected first");

    queue.Enqueue({"second.png"});
    queue.Enqueue({"third.png"});
    RtPbrSurvey::ScreenshotRequest blockedRequest;
    passed &= Check(!queue.BeginNextCapture(blockedRequest), "pending capture blocks a second submission");

    passed &= Check(queue.CompletePending({activeRequest.path, true, {}, 640, 480}), "first request completes");
    RtPbrSurvey::ScreenshotRequest secondRequest;
    passed &= Check(queue.BeginNextCapture(secondRequest), "second request begins after first completion");
    passed &= Check(secondRequest.path == "second.png", "second request preserves FIFO order");
    passed &= Check(queue.CompletePending({secondRequest.path, false, "write failed"}), "second request records failure");

    RtPbrSurvey::ScreenshotRequest thirdRequest;
    passed &= Check(queue.BeginNextCapture(thirdRequest), "third request begins after failed second request");
    passed &= Check(thirdRequest.path == "third.png", "third request preserves FIFO order");
    passed &= Check(queue.CompletePending({thirdRequest.path, true, {}, 1280, 720}), "third request completes");

    const auto first = queue.ConsumeResult();
    const auto second = queue.ConsumeResult();
    const auto third = queue.ConsumeResult();
    passed &= Check(first.has_value() && first->path == "first.png" && first->succeeded && first->width == 640,
                    "first success result is first");
    passed &= Check(second.has_value() && second->path == "second.png" && !second->succeeded &&
                        second->error == "write failed",
                    "second failure result is second");
    passed &= Check(third.has_value() && third->path == "third.png" && third->succeeded && third->height == 720,
                    "third success result is third");
    passed &= Check(!queue.ConsumeResult().has_value(), "all results are consumed once");
    return passed;
}
} // namespace

int main()
{
    return TestPendingCaptureQueuesRequestsInFifoOrder() ? 0 : 1;
}
