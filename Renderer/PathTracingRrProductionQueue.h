#pragma once

#include <array>
#include <cstdint>
#include <optional>

namespace Engine
{
// CPU-side ownership for the experimental split production/presentation path.
// The renderer owns actual resources, queue submission and summary validation.
// Call the transitions from the render thread, in queue submission order.
class PathTracingRrProductionQueue
{
public:
    static constexpr unsigned int SlotCount = 3;

    struct Ticket
    {
        unsigned int slot;
        uint64_t serial;
        uint64_t epoch;
        uint64_t previousEvaluationSerial; // Zero means the SDK history must reset.
        bool operator==(const Ticket&) const = default;
    };

    struct Validation
    {
        bool inputVerified;
        bool sdkSucceeded;
        bool outputVerified;
    };

    // maxAhead counts recording, submitted and verified-but-unpresented work.
    PathTracingRrProductionQueue(unsigned int maxAhead, uint64_t epoch);

    std::optional<Ticket> Reserve();
    bool Submit(const Ticket& ticket, uint64_t productionFence);
    // Use if recording was abandoned before GPU submission.
    bool DiscardRecording(const Ticket& ticket);
    bool Validate(const Ticket& ticket, Validation result);

    std::optional<Ticket> NextPresentation() const;
    std::optional<Ticket> RetainedOutput() const;
    // Register every submitted read, even if Present later fails.
    bool TrackRead(const Ticket& ticket, uint64_t presentationFence);
    // Commit only after Present returns S_OK. An occluded or failed Present does not commit.
    bool MarkPresented(const Ticket& ticket);

    // Both fence values belong to their own monotonically increasing domains.
    // A device-removal sentinel in either domain permanently stops this queue.
    bool ObserveFences(uint64_t completedProduction, uint64_t completedPresentation);
    bool AdvanceEpoch(uint64_t epoch);

private:
    enum class State
    {
        Free,
        Recording,
        Submitted,
        Ready,
        Retained,
        Retiring
    };

    struct Slot
    {
        State state = State::Free;
        Ticket ticket = {};
        uint64_t productionFence = 0;
        uint64_t lastReadFence = 0;
    };

    Slot* Find(const Ticket& ticket);
    const Slot* Oldest(State state) const;
    void RejectFrom(uint64_t serial);
    void ReclaimRetired();

    std::array<Slot, SlotCount> m_slots = {};
    unsigned int m_maxAhead;
    uint64_t m_epoch;
    uint64_t m_lastSerial = 0;
    uint64_t m_previousEvaluationSerial = 0;
    uint64_t m_lastProductionFence = 0;
    uint64_t m_lastPresentationFence = 0;
    uint64_t m_completedProduction = 0;
    uint64_t m_completedPresentation = 0;
    bool m_draining = false;
    bool m_deviceRemoved = false;
};
} // namespace Engine
