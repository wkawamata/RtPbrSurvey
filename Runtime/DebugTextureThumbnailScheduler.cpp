#include "stdafx.h"

#include "Runtime/DebugTextureThumbnailScheduler.h"

#include <algorithm>

namespace RtPbrSurvey
{
void DebugTextureThumbnailScheduler::Request(const Engine::DebugResourceViewDescriptor& descriptor, bool selected)
{
    if (descriptor.viewKind != Engine::DebugResourceViewKind::Texture &&
        (!descriptor.imageLayout.IsValid(descriptor.elementCount, descriptor.elementStride) ||
         descriptor.sourceDescriptorName.empty()))
    {
        return;
    }

    const auto existing = std::find_if(m_pendingRequests.begin(),
                                       m_pendingRequests.end(),
                                       [&descriptor](const RequestState& request)
                                       { return request.resourceName == descriptor.resourceName; });
    if (existing != m_pendingRequests.end())
    {
        existing->selected = existing->selected || selected;
        existing->semantic = descriptor.semantic;
        existing->viewKind = descriptor.viewKind;
        return;
    }
    m_pendingRequests.push_back({descriptor.resourceName, descriptor.semantic, descriptor.viewKind, selected});
}

std::array<DebugTextureThumbnailSlotPlan, DebugTextureThumbnailScheduler::kSlotCount>
DebugTextureThumbnailScheduler::BuildFramePlan(uint64_t frameIndex)
{
    m_currentFrame = frameIndex;
    std::array<DebugTextureThumbnailSlotPlan, kSlotCount> plan;
    if (m_pendingRequests.empty())
    {
        m_slots = {};
        return plan;
    }

    std::vector<RequestState> requests = std::move(m_pendingRequests);
    m_pendingRequests.clear();
    const auto selected = std::find_if(
        requests.begin(), requests.end(), [](const RequestState& request) { return request.selected; });
    std::vector<const RequestState*> desired;
    if (selected != requests.end())
    {
        desired.push_back(&*selected);
    }

    std::vector<const RequestState*> visible;
    for (const RequestState& request : requests)
    {
        if (!request.selected)
        {
            visible.push_back(&request);
        }
    }

    const size_t availableVisibleSlots = kSlotCount - desired.size();
    const bool hasEmptySlot = std::any_of(
        m_slots.begin(), m_slots.end(), [](const SlotState& slot) { return !slot.active; });
    const bool rotateVisible = hasEmptySlot || frameIndex % kUnselectedRefreshInterval == 0;
    if (!rotateVisible)
    {
        for (const SlotState& slot : m_slots)
        {
            const auto request = std::find_if(visible.begin(),
                                              visible.end(),
                                              [&slot](const RequestState* candidate)
                                              { return candidate->resourceName == slot.resourceName; });
            if (request != visible.end() && desired.size() < kSlotCount)
            {
                desired.push_back(*request);
            }
        }
    }

    if (!visible.empty())
    {
        size_t visited = 0;
        while (desired.size() < kSlotCount && visited < visible.size())
        {
            const RequestState* request = visible[(m_roundRobinCursor + visited) % visible.size()];
            const bool alreadyDesired = std::any_of(desired.begin(),
                                                    desired.end(),
                                                    [request](const RequestState* candidate)
                                                    { return candidate->resourceName == request->resourceName; });
            if (!alreadyDesired)
            {
                desired.push_back(request);
            }
            ++visited;
        }
        if (rotateVisible && availableVisibleSlots > 0)
        {
            m_roundRobinCursor = (m_roundRobinCursor + availableVisibleSlots) % visible.size();
        }
    }

    std::array<bool, kSlotCount> assigned = {};
    std::vector<std::pair<const RequestState*, size_t>> assignments;
    assignments.reserve(desired.size());
    for (const RequestState* request : desired)
    {
        const auto existing = std::find_if(m_slots.begin(),
                                           m_slots.end(),
                                           [request](const SlotState& slot)
                                           { return slot.active && slot.resourceName == request->resourceName; });
        if (existing == m_slots.end())
        {
            continue;
        }
        const size_t slotIndex = static_cast<size_t>(std::distance(m_slots.begin(), existing));
        assigned[slotIndex] = true;
        assignments.push_back({request, slotIndex});
    }
    for (const RequestState* request : desired)
    {
        const bool alreadyAssigned = std::any_of(assignments.begin(),
                                                 assignments.end(),
                                                 [request](const auto& assignment)
                                                 { return assignment.first == request; });
        if (alreadyAssigned)
        {
            continue;
        }
        const auto freeSlot = std::find(assigned.begin(), assigned.end(), false);
        if (freeSlot == assigned.end())
        {
            break;
        }
        const size_t slotIndex = static_cast<size_t>(std::distance(assigned.begin(), freeSlot));
        assigned[slotIndex] = true;
        assignments.push_back({request, slotIndex});
    }

    for (size_t slotIndex = 0; slotIndex < kSlotCount; ++slotIndex)
    {
        if (!assigned[slotIndex])
        {
            m_slots[slotIndex] = {};
            continue;
        }
        const auto assignment = std::find_if(assignments.begin(),
                                             assignments.end(),
                                             [slotIndex](const auto& candidate)
                                             { return candidate.second == slotIndex; });
        const RequestState& request = *assignment->first;
        SlotState& slot = m_slots[slotIndex];
        const bool changed = !slot.active || slot.resourceName != request.resourceName;
        const bool update = changed || request.selected ||
            frameIndex - slot.lastUpdateFrame >= kUnselectedRefreshInterval;
        if (changed)
        {
            slot.readyFrame = frameIndex + 1;
        }
        if (update)
        {
            slot.lastUpdateFrame = frameIndex;
        }
        slot.active = true;
        slot.resourceName = request.resourceName;
        slot.semantic = request.semantic;
        slot.viewKind = request.viewKind;
        plan[slotIndex] = {true, update, slot.resourceName, slot.semantic, slot.viewKind};
    }
    return plan;
}

std::optional<size_t> DebugTextureThumbnailScheduler::FindReadySlot(const std::string& resourceName) const
{
    for (size_t slotIndex = 0; slotIndex < m_slots.size(); ++slotIndex)
    {
        const SlotState& slot = m_slots[slotIndex];
        if (slot.active && slot.resourceName == resourceName && m_currentFrame >= slot.readyFrame)
        {
            return slotIndex;
        }
    }
    return std::nullopt;
}

void DebugTextureThumbnailScheduler::Reset()
{
    m_pendingRequests.clear();
    m_slots = {};
    m_roundRobinCursor = 0;
    m_currentFrame = 0;
}
} // namespace RtPbrSurvey
