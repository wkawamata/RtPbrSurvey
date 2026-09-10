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
    std::vector<RequestState> requests = std::move(m_pendingRequests);
    m_pendingRequests.clear();
    const auto selected = std::find_if(
        requests.begin(), requests.end(), [](const RequestState& request) { return request.selected; });
    std::vector<RequestState> desired;
    desired.reserve(kSlotCount);
    if (selected != requests.end())
    {
        desired.push_back(*selected);
    }

    const auto isDesired = [&desired](const std::string& resourceName)
    {
        return std::any_of(desired.begin(), desired.end(), [&resourceName](const RequestState& request)
                           { return request.resourceName == resourceName; });
    };
    const auto findRequest = [&requests](const std::string& resourceName)
    {
        return std::find_if(requests.begin(), requests.end(), [&resourceName](const RequestState& request)
                            { return request.resourceName == resourceName; });
    };

    for (const SlotState& slot : m_slots)
    {
        if (!slot.active || desired.size() >= kSlotCount || isDesired(slot.resourceName))
        {
            continue;
        }
        const auto request = findRequest(slot.resourceName);
        if (request != requests.end())
        {
            desired.push_back(*request);
        }
    }

    for (const RequestState& request : requests)
    {
        if (desired.size() >= kSlotCount)
        {
            break;
        }
        if (!isDesired(request.resourceName))
        {
            desired.push_back(request);
        }
    }

    for (const SlotState& slot : m_slots)
    {
        if (!slot.active || desired.size() >= kSlotCount || isDesired(slot.resourceName) ||
            frameIndex - slot.lastRequestFrame > kRequestGraceFrames)
        {
            continue;
        }
        desired.push_back({slot.resourceName, slot.semantic, slot.viewKind, false});
    }

    std::array<bool, kSlotCount> assigned = {};
    std::vector<std::pair<const RequestState*, size_t>> assignments;
    assignments.reserve(desired.size());
    for (const RequestState& request : desired)
    {
        const auto existing = std::find_if(m_slots.begin(),
                                           m_slots.end(),
                                           [&request](const SlotState& slot)
                                           { return slot.active && slot.resourceName == request.resourceName; });
        if (existing == m_slots.end())
        {
            continue;
        }
        const size_t slotIndex = static_cast<size_t>(std::distance(m_slots.begin(), existing));
        assigned[slotIndex] = true;
        assignments.push_back({&request, slotIndex});
    }
    for (const RequestState& request : desired)
    {
        const bool alreadyAssigned = std::any_of(assignments.begin(),
                                                 assignments.end(),
                                                 [&request](const auto& assignment)
                                                 { return assignment.first == &request; });
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
        assignments.push_back({&request, slotIndex});
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
        const auto currentRequest = findRequest(request.resourceName);
        const bool requested = currentRequest != requests.end();
        const bool selectedRequest = requested && currentRequest->selected;
        const bool update = requested &&
            (changed || selectedRequest || frameIndex - slot.lastUpdateFrame >= kUnselectedRefreshInterval);
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
        if (requested)
        {
            slot.lastRequestFrame = frameIndex;
        }
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
    m_currentFrame = 0;
}
} // namespace RtPbrSurvey
