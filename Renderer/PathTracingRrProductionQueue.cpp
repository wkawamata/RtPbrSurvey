#include "PathTracingRrProductionQueue.h"

#include <limits>
#include <stdexcept>

namespace Engine
{
namespace
{
constexpr uint64_t InvalidFence = (std::numeric_limits<uint64_t>::max)();
}

PathTracingRrProductionQueue::PathTracingRrProductionQueue(unsigned int maxAhead, uint64_t epoch)
    : m_maxAhead(maxAhead), m_epoch(epoch)
{
    if (maxAhead == 0 || maxAhead >= SlotCount)
    {
        throw std::invalid_argument("RR ahead limit must be one or two");
    }
}

std::optional<PathTracingRrProductionQueue::Ticket> PathTracingRrProductionQueue::Reserve()
{
    if (m_deviceRemoved || m_draining || m_lastSerial == (std::numeric_limits<uint64_t>::max)())
    {
        return std::nullopt;
    }
    unsigned int readyOrPending = 0;
    unsigned int freeSlot = SlotCount;
    for (unsigned int i = 0; i < SlotCount; ++i)
    {
        const State state = m_slots[i].state;
        if (state == State::Recording)
        {
            return std::nullopt;
        }
        if (state == State::Submitted || state == State::Ready)
        {
            ++readyOrPending;
        }
        if (state == State::Free && freeSlot == SlotCount)
        {
            freeSlot = i;
        }
    }
    if (readyOrPending >= m_maxAhead || freeSlot == SlotCount)
    {
        return std::nullopt;
    }
    Slot& slot = m_slots[freeSlot];
    slot.ticket = {freeSlot, ++m_lastSerial, m_epoch, m_previousEvaluationSerial};
    slot.state = State::Recording;
    return slot.ticket;
}

PathTracingRrProductionQueue::Slot* PathTracingRrProductionQueue::Find(const Ticket& ticket)
{
    if (m_deviceRemoved || ticket.slot >= SlotCount || ticket.epoch != m_epoch)
    {
        return nullptr;
    }
    Slot& slot = m_slots[ticket.slot];
    return slot.state != State::Free && slot.ticket == ticket ? &slot : nullptr;
}

const PathTracingRrProductionQueue::Slot* PathTracingRrProductionQueue::Oldest(State state) const
{
    const Slot* oldest = nullptr;
    for (const Slot& slot : m_slots)
    {
        if (slot.state == state && (!oldest || slot.ticket.serial < oldest->ticket.serial))
        {
            oldest = &slot;
        }
    }
    return oldest;
}

bool PathTracingRrProductionQueue::Submit(const Ticket& ticket, uint64_t productionFence)
{
    Slot* slot = Find(ticket);
    if (!slot || slot->state != State::Recording || productionFence == InvalidFence ||
        productionFence <= m_lastProductionFence || productionFence <= m_completedProduction)
    {
        return false;
    }
    slot->productionFence = productionFence;
    slot->state = State::Submitted;
    m_lastProductionFence = productionFence;
    m_previousEvaluationSerial = ticket.serial;
    return true;
}

bool PathTracingRrProductionQueue::DiscardRecording(const Ticket& ticket)
{
    const Slot* slot = Find(ticket);
    if (!slot || slot->state != State::Recording)
    {
        return false;
    }
    RejectFrom(ticket.serial);
    return true;
}

bool PathTracingRrProductionQueue::Validate(const Ticket& ticket, Validation result)
{
    Slot* slot = Find(ticket);
    if (!slot || slot->state != State::Submitted || slot != Oldest(State::Submitted) ||
        slot->productionFence > m_completedProduction)
    {
        return false;
    }
    if (result.inputVerified && result.sdkSucceeded && result.outputVerified)
    {
        slot->state = State::Ready;
    }
    else
    {
        RejectFrom(ticket.serial);
    }
    return true;
}

std::optional<PathTracingRrProductionQueue::Ticket> PathTracingRrProductionQueue::NextPresentation() const
{
    const Slot* slot = Oldest(State::Ready);
    return !m_deviceRemoved && slot ? std::optional<Ticket>(slot->ticket) : std::nullopt;
}

std::optional<PathTracingRrProductionQueue::Ticket> PathTracingRrProductionQueue::RetainedOutput() const
{
    const Slot* slot = Oldest(State::Retained);
    return !m_deviceRemoved && slot ? std::optional<Ticket>(slot->ticket) : std::nullopt;
}

bool PathTracingRrProductionQueue::TrackRead(const Ticket& ticket, uint64_t presentationFence)
{
    Slot* slot = Find(ticket);
    if (!slot || (slot->state != State::Ready && slot->state != State::Retained) ||
        presentationFence == InvalidFence || presentationFence <= m_completedPresentation ||
        presentationFence < m_lastPresentationFence)
    {
        return false;
    }
    m_lastPresentationFence = presentationFence;
    slot->lastReadFence = presentationFence;
    return true;
}

bool PathTracingRrProductionQueue::MarkPresented(const Ticket& ticket)
{
    Slot* slot = Find(ticket);
    if (!slot || slot->state != State::Ready || slot != Oldest(State::Ready) || slot->lastReadFence == 0)
    {
        return false;
    }
    for (Slot& previous : m_slots)
    {
        if (previous.state == State::Retained)
        {
            previous.state = State::Retiring;
        }
    }
    slot->state = State::Retained;
    ReclaimRetired();
    return true;
}

bool PathTracingRrProductionQueue::ObserveFences(uint64_t completedProduction, uint64_t completedPresentation)
{
    if (completedProduction == InvalidFence || completedPresentation == InvalidFence)
    {
        m_deviceRemoved = true;
    }
    if (m_deviceRemoved || completedProduction < m_completedProduction || completedPresentation < m_completedPresentation)
    {
        return false;
    }
    m_completedProduction = completedProduction;
    m_completedPresentation = completedPresentation;
    ReclaimRetired();
    return true;
}

bool PathTracingRrProductionQueue::AdvanceEpoch(uint64_t epoch)
{
    if (m_deviceRemoved || epoch <= m_epoch)
    {
        return false;
    }
    m_epoch = epoch;
    RejectFrom(0);
    return true;
}

void PathTracingRrProductionQueue::RejectFrom(uint64_t serial)
{
    for (Slot& slot : m_slots)
    {
        if (slot.state != State::Free && slot.ticket.serial >= serial)
        {
            slot.state = State::Retiring;
        }
    }
    m_previousEvaluationSerial = 0;
    m_draining = true;
    ReclaimRetired();
}

void PathTracingRrProductionQueue::ReclaimRetired()
{
    bool retirementPending = false;
    for (Slot& slot : m_slots)
    {
        if (slot.state != State::Retiring)
        {
            continue;
        }
        if (slot.productionFence <= m_completedProduction && slot.lastReadFence <= m_completedPresentation)
        {
            slot = {};
        }
        else
        {
            retirementPending = true;
        }
    }
    m_draining = m_draining && (retirementPending || m_completedProduction < m_lastProductionFence);
}
} // namespace Engine
