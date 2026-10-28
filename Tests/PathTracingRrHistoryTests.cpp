#include "Renderer/PathTracingRrHistory.h"

#include <iostream>
#include <limits>
#include <stdexcept>

namespace
{
using Key = Engine::PathTracingRrHistoryKey;
using History = Engine::PathTracingRrHistory;
using Action = Engine::PathTracingRrHistoryAction;
using Reason = Engine::PathTracingRrHistoryReason;
using Completion = Engine::PathTracingRrCompletion;

Key Frame(uint64_t id = 1)
{
    Key key;
    key.frameId = id;
    key.sampleStartIndex = static_cast<uint32_t>(id - 1);
    key.samplingSignature = 7;
    key.resetToken = 1;
    key.renderWidth = key.outputWidth = 256;
    key.renderHeight = key.outputHeight = 144;
    key.viewProjection = key.previousViewProjection = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    return key;
}

void Check(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

void Complete(History& history, const Key& key, const void* allocation)
{
    const auto request = history.Begin(key, allocation, true, true);
    Check(request.action == Action::Evaluate && request.ticket != 0, "Expected an evaluation");
    Check(history.Finish(request.ticket, true, true) == Completion::Completed, "Expected GPU completion");
}
} // namespace

int main()
{
    try
    {
        int allocation, replacement;
        History history;
        auto first = Frame();
        auto request = history.Begin(first, &allocation, true, true);
        Check(request.action == Action::Evaluate && request.reset && request.reason == Reason::FirstEvaluation,
              "First evaluation must reset");
        Check(!history.CanPresent(first, &allocation), "Recording alone cannot present RR");
        Check(history.Finish(request.ticket, true, false) == Completion::PendingGpu,
              "SDK success is not GPU completion");
        Check(!history.CanPresent(first, &allocation), "Pending GPU output cannot be presented");
        const auto busy = history.Begin(Frame(2), &replacement, true, true);
        Check(busy.action == Action::NativeFallback && busy.reason == Reason::PendingEvaluation,
              "Pending production cannot be replaced");
        Check(history.Finish(request.ticket + 1, false, true) == Completion::StaleTicket,
              "Stale completion must not poison pending work");
        Check(history.Finish(request.ticket, true, true) == Completion::Completed, "Valid completion rejected");
        Check(history.CanPresent(first, &allocation) && !history.CanPresent(Frame(2), &allocation) &&
                  !history.CanPresent(first, &replacement),
              "Output requires exact production and allocation");
        const auto paused = history.Begin(first, &allocation, true, true);
        Check(paused.action == Action::ReuseOutput && paused.ticket == 0 && !paused.reset,
              "Paused redraw must reuse without an SDK evaluation");

        auto second = Frame(2);
        second.viewProjection[12] = .25f;
        request = history.Begin(second, &replacement, true, true);
        Check(!request.reset && request.reason == Reason::Continuous,
              "Camera motion with matching history is continuous");
        Check(!history.CanPresent(first, &allocation), "New production must not expose previous RR as current");
        Check(history.Finish(request.ticket, true, true) == Completion::Completed, "Second output rejected");
        auto third = Frame(3);
        third.previousViewProjection = second.viewProjection;
        request = history.Begin(third, &allocation, true, true);
        Check(!request.reset, "Last successfully evaluated camera must be used");
        Check(history.Finish(request.ticket, false, false) == Completion::Failed, "SDK failure not invalidated");
        Check(!history.CanPresent(second, &replacement), "Failure must fall back");
        const auto failedTicket = request.ticket;
        request = history.Begin(Frame(4), &allocation, true, true);
        Check(request.reset && request.reason == Reason::FirstEvaluation,
              "After failure the next evaluation must reset");
        Check(history.Finish(failedTicket, true, true) == Completion::StaleTicket,
              "Old success must not replace newer packet");
        Check(history.Finish(request.ticket, true, true) == Completion::Completed,
              "New success lost after stale completion");
        auto changed = Frame(4);
        changed.resetToken = 2;
        Check(history.Begin(changed, &allocation, true, true).action == Action::NativeFallback,
              "Changing metadata cannot relabel an old production");

        const auto resetCase = [&](auto modify, Reason reason)
        {
            History state;
            Complete(state, Frame(), &allocation);
            auto key = Frame(2);
            modify(key);
            const auto decision = state.Begin(key, &replacement, true, true);
            Check(decision.action == Action::Evaluate && decision.reset && decision.reason == reason,
                  "Discontinuity must reset before SDK evaluation");
        };
        resetCase([](Key& key) { ++key.resetToken; }, Reason::ResetTokenChanged);
        resetCase([](Key& key) { ++key.renderWidth; }, Reason::DimensionsChanged);
        resetCase([](Key& key) { ++key.outputHeight; }, Reason::DimensionsChanged);
        resetCase([](Key& key) { ++key.samplingSignature; }, Reason::SamplingChanged);
        resetCase([](Key& key) { ++key.frameId; }, Reason::FrameGap);
        resetCase([](Key& key) { ++key.sampleStartIndex; }, Reason::SampleGap);
        resetCase([](Key& key) { key.previousViewProjection[12] = .5f; }, Reason::CameraHistoryMismatch);

        for (bool requested : {false, true})
        {
            History state;
            auto pending = state.Begin(Frame(), &allocation, true, true);
            const auto rejected = state.Begin(Frame(2), &replacement, requested, false);
            Check(rejected.action == Action::NativeFallback &&
                      rejected.reason == (requested ? Reason::InputsRejected : Reason::Disabled),
                  "Disabled/rejected inputs need fallback");
            Check(state.Finish(pending.ticket, true, true) == Completion::StaleTicket,
                  "Disabled pending output must not later become presentable");
            Check(!state.CanPresent(Frame(), &allocation), "Cancelled output presented");
            const auto fresh = state.Begin(Frame(2), &replacement, true, true);
            Check(fresh.reset && fresh.ticket != pending.ticket, "Invalidation must not recycle ticket identity");
        }
        History invalid;
        auto bad = Frame();
        bad.viewProjection.fill(0);
        Check(invalid.Begin(bad, &allocation, true, true).reason == Reason::InvalidKey, "Singular camera admitted");
        bad = Frame();
        bad.viewProjection[0] = std::numeric_limits<float>::quiet_NaN();
        Check(invalid.Begin(bad, &allocation, true, true).reason == Reason::InvalidKey, "NaN camera admitted");
        Check(invalid.Begin(Frame(), nullptr, true, true).reason == Reason::InvalidKey, "Missing allocation admitted");
        bad = Frame();
        bad.sampleStartIndex = std::numeric_limits<uint32_t>::max();
        Check(invalid.Begin(bad, &allocation, true, true).reason == Reason::InvalidKey,
              "Exhausted sample sequence admitted");

        History batch;
        auto batchFirst = Frame();
        batchFirst.samplesPerFrame = 4;
        Complete(batch, batchFirst, &allocation);
        auto batchNext = Frame(2);
        batchNext.samplesPerFrame = 4;
        batchNext.sampleStartIndex = 4;
        batchNext.jitterPixels = {.25f, -.25f};
        const auto batchRequest = batch.Begin(batchNext, &replacement, true, true);
        Check(batchRequest.reason == Reason::Continuous && !batchRequest.reset, "Batch stride/jitter lost history");
        batch.Finish(batchRequest.ticket, true, true);
        auto changedBatch = Frame(3);
        changedBatch.samplesPerFrame = 16;
        changedBatch.sampleStartIndex = 8;
        Check(batch.Begin(changedBatch, &allocation, true, true).reason == Reason::SamplingChanged,
              "Batch size change must reset history");
        History badBatch;
        auto overflow = Frame();
        overflow.samplesPerFrame = 4;
        overflow.sampleStartIndex = std::numeric_limits<uint32_t>::max() - 2;
        Check(badBatch.Begin(overflow, &allocation, true, true).reason == Reason::InvalidKey,
              "Batch overflow admitted");
        overflow = Frame();
        overflow.jitterPixels[0] = std::numeric_limits<float>::quiet_NaN();
        Check(badBatch.Begin(overflow, &allocation, true, true).reason == Reason::InvalidKey, "NaN jitter admitted");
        History exhausted;
        auto last = Frame();
        last.frameId = std::numeric_limits<uint64_t>::max();
        Complete(exhausted, last, &allocation);
        Check(exhausted.Begin(Frame(), &replacement, true, true).action == Action::NativeFallback,
              "Production sequence wrap must not be continuous");
        std::cout << "RR history, pending completion, reset and presentation transitions passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
