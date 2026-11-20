#include "Renderer/PathTracingRrProductionQueue.h"

#include <iostream>
#include <limits>
#include <stdexcept>

namespace
{
using Queue = Engine::PathTracingRrProductionQueue;
using Ticket = Queue::Ticket;
constexpr Queue::Validation Valid = {true, true, true};

void Check(bool condition, const char* message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

Ticket Reserve(Queue& queue)
{
    const auto ticket = queue.Reserve();
    Check(ticket.has_value(), "Expected a free RR slot");
    return *ticket;
}

void TestSingleProduction()
{
    Queue queue(1, 10);
    const Ticket first = Reserve(queue);
    Check(first.previousEvaluationSerial == 0, "First production must reset history");
    Check(!queue.Reserve(), "Only one slot may be recording");
    Check(queue.Submit(first, 7), "First production should submit");
    Check(!queue.Reserve() && !queue.NextPresentation(), "GPU work is pending");
    Check(queue.ObserveFences(6, 80), "Observe progress on both fence domains");
    Check(!queue.Validate(first, Valid), "Display completion cannot substitute for production completion");
    Check(queue.ObserveFences(7, 80) && queue.Validate(first, Valid), "Validate completed production");
    Check(queue.NextPresentation() == first && !queue.Reserve(), "Ready work still counts toward the limit");
    Check(!queue.MarkPresented(first), "Present requires a tracked GPU read");
    Check(queue.TrackRead(first, 81), "Protect the image selected for presentation");
    Check(queue.MarkPresented(first), "Mark the successful Present");
    Check(!queue.NextPresentation() && queue.RetainedOutput() == first, "Do not present the same image as new twice");
    const Ticket second = Reserve(queue);
    Check(first.slot != second.slot && second.previousEvaluationSerial == first.serial,
          "Keep the retained output and preserve SDK evaluation order");
}

void TestTwoProductionsAndReaderRetirement()
{
    Queue queue(2, 1);
    const Ticket first = Reserve(queue);
    Check(queue.Submit(first, 1) && queue.ObserveFences(1, 0) && queue.Validate(first, Valid),
          "Prepare a retained image");
    Check(queue.TrackRead(first, 10) && queue.MarkPresented(first), "Present first image");
    Check(queue.TrackRead(first, 13), "A later capture extends the retained image lifetime");

    const Ticket second = Reserve(queue);
    Check(queue.Submit(second, 2), "Submit second image");
    const Ticket third = Reserve(queue);
    Check(third.previousEvaluationSerial == second.serial && !queue.Reserve(),
          "Use most recently submitted evaluation as the history predecessor");
    Check(!queue.Submit(third, 2) && queue.Submit(third, 3), "Production fence must advance");

    Check(queue.ObserveFences(3, 10), "Both new images have finished GPU work");
    Check(!queue.Validate(third, Valid), "Validate in submission order");
    Check(queue.Validate(second, Valid) && queue.Validate(third, Valid), "Validate in order");
    Check(queue.TrackRead(third, 14) && !queue.MarkPresented(third), "Prevent out-of-order presentation");
    Check(queue.NextPresentation() == second, "Choose the oldest new verified image");
    Check(!queue.TrackRead(second, 13), "Reject a fence registered out of submission order");
    Check(queue.TrackRead(second, 15), "Track a read even if Present fails");
    Check(queue.NextPresentation() == second && queue.RetainedOutput() == first,
          "A failed Present does not advance the image");
    Check(queue.TrackRead(second, 16) && queue.MarkPresented(second), "Retry and present in order");
    Check(!queue.Reserve(), "Formerly retained image still has a GPU reader");
    Check(queue.ObserveFences(3, 13), "Finish the last GPU read of the first image");
    const Ticket fourth = Reserve(queue);
    Check(fourth.slot == first.slot && fourth.serial != first.serial, "Reuse only a retired physical slot");
    Check(!queue.TrackRead(first, 17), "A stale ticket cannot extend the new owner's lifetime");
}

void TestRejectAndDrain()
{
    for (const Queue::Validation invalid : {Queue::Validation{false, true, true},
                                            Queue::Validation{true, false, true},
                                            Queue::Validation{true, true, false}})
    {
        Queue queue(2, 7);
        const Ticket retained = Reserve(queue);
        Check(queue.Submit(retained, 1) && queue.ObserveFences(1, 0) && queue.Validate(retained, Valid) &&
                  queue.TrackRead(retained, 10) && queue.MarkPresented(retained),
              "Prepare a verified fallback");
        const Ticket first = Reserve(queue);
        Check(queue.Submit(first, 2), "Submit the image that will be rejected");
        const Ticket dependent = Reserve(queue);
        Check(queue.Submit(dependent, 3), "Submit a dependent image");
        Check(queue.ObserveFences(2, 0) && queue.Validate(first, invalid), "Reject failed summary or SDK");
        Check(!queue.NextPresentation() && queue.RetainedOutput() == retained, "Only verified fallback survives");
        Check(!queue.Reserve() && !queue.Validate(dependent, Valid), "Drain and discard dependent output");
        Check(queue.ObserveFences(3, 0), "Finish the rejected dependency chain");
        const Ticket retry = Reserve(queue);
        Check(retry.previousEvaluationSerial == 0 && retry.epoch == retained.epoch,
              "An in-epoch retry starts with an SDK reset");
    }
}

void TestEpochChangeAndFailedPresentReader()
{
    Queue queue(2, 5);
    const Ticket ready = Reserve(queue);
    Check(queue.Submit(ready, 1) && queue.ObserveFences(1, 0) && queue.Validate(ready, Valid),
          "Prepare a verified image");
    Check(queue.TrackRead(ready, 50), "Even an unsuccessful Present submitted a GPU read");
    const Ticket pending = Reserve(queue);
    Check(queue.Submit(pending, 2), "Leave one production pending");
    Check(queue.AdvanceEpoch(6), "Change epoch during resize or backend switch");
    Check(!queue.AdvanceEpoch(5) && !queue.NextPresentation() && !queue.RetainedOutput(),
          "Hide old images and reject backward epochs");
    Check(!queue.TrackRead(ready, 51) && !queue.Validate(pending, Valid), "Reject stale epoch callbacks");
    Check(queue.ObserveFences(1, 50) && !queue.Reserve(), "Production fence still protects the pending work");
    Check(queue.ObserveFences(2, 50), "Both domains are drained");
    const Ticket next = Reserve(queue);
    Check(next.epoch == 6 && next.previousEvaluationSerial == 0, "First new epoch image resets SDK history");

    Queue reverse(2, 5);
    const Ticket output = Reserve(reverse);
    Check(reverse.Submit(output, 1) && reverse.ObserveFences(1, 0) && reverse.Validate(output, Valid) &&
              reverse.TrackRead(output, 50) && reverse.AdvanceEpoch(6),
          "Resize with a GPU read still pending");
    Check(!reverse.Reserve(), "Production completion alone does not retire presentation resources");
    Check(reverse.ObserveFences(1, 50) && reverse.Reserve().has_value(), "Free resources after the last read");
}

void TestDiscardAndDeviceRemoval()
{
    Queue preceding(2, 1);
    const Ticket active = Reserve(preceding);
    Check(preceding.Submit(active, 1), "Keep an earlier GPU submission pending");
    const Ticket aborted = Reserve(preceding);
    Check(preceding.DiscardRecording(aborted) && !preceding.Reserve(),
          "Wait for the earlier submission before starting a new SDK history");
    Check(preceding.ObserveFences(1, 0) && preceding.Validate(active, Valid),
          "An earlier admissible image survives the abandoned recording");
    Check(Reserve(preceding).previousEvaluationSerial == 0, "Start a new SDK history after draining");

    Queue queue(1, 1);
    const Ticket discarded = Reserve(queue);
    Check(queue.DiscardRecording(discarded), "Release a recording never submitted to the GPU");
    const Ticket next = Reserve(queue);
    Check(next.slot == discarded.slot && next.serial != discarded.serial && next.previousEvaluationSerial == 0,
          "An abandoned SDK recording requires a new ticket and history reset");
    Check(!queue.Submit(discarded, 1) && queue.Submit(next, 1), "Reject the abandoned ticket");
    Check(queue.ObserveFences(1, 0) && queue.Validate(next, Valid), "Prepare one ready output");
    Check(!queue.ObserveFences(0, 0) && queue.NextPresentation() == next, "Ignore a regressed fence report");
    Check(!queue.ObserveFences(1, (std::numeric_limits<uint64_t>::max)()), "Device removal must latch");
    Check(!queue.NextPresentation() && !queue.Reserve() && !queue.AdvanceEpoch(2) && !queue.ObserveFences(1, 50),
          "Never resume normal publication or reuse after device removal");
}
} // namespace

int main()
{
    try
    {
        TestSingleProduction();
        TestTwoProductionsAndReaderRetirement();
        TestRejectAndDrain();
        TestEpochChangeAndFailedPresentReader();
        TestDiscardAndDeviceRemoval();
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
