#include "stdafx.h"

#include "Runtime/DebugLine.h"

namespace RtPbrSurvey
{
namespace
{
static constexpr uint32_t kIndexBits = 11;
static constexpr uint32_t kIndexMask = (1u << kIndexBits) - 1u;
static constexpr uint32_t kGenerationMask = (1u << (32u - kIndexBits)) - 1u;
} // namespace

DebugLineHandle DebugLineRegistry::Add(const DebugLineDesc& desc)
{
    for (uint32_t index = 0; index < m_slots.size(); index++)
    {
        Slot& slot = m_slots[index];
        if (!slot.occupied)
        {
            slot.desc = desc;
            slot.occupied = true;
            return MakeHandle(index, slot.generation);
        }
    }
    return kInvalidDebugLineHandle;
}

bool DebugLineRegistry::Update(DebugLineHandle handle, const DebugLineDesc& desc)
{
    Slot* slot = Find(handle);
    if (!slot)
    {
        return false;
    }
    slot->desc = desc;
    return true;
}

void DebugLineRegistry::Remove(DebugLineHandle handle)
{
    Slot* slot = Find(handle);
    if (!slot)
    {
        return;
    }
    slot->occupied = false;
    slot->generation = (slot->generation % kGenerationMask) + 1u;
}

void DebugLineRegistry::Clear()
{
    for (Slot& slot : m_slots)
    {
        if (slot.occupied)
        {
            slot.occupied = false;
            slot.generation = (slot.generation % kGenerationMask) + 1u;
        }
    }
}

DebugLineVertexGroups DebugLineRegistry::AssembleVertices() const
{
    DebugLineVertexGroups result;
    result.depthTested.reserve(kMaxHostDebugLines * 2);
    result.overlay.reserve(kMaxHostDebugLines * 2);
    for (const Slot& slot : m_slots)
    {
        if (!slot.occupied || !slot.desc.visible)
        {
            continue;
        }
        std::vector<DebugLineVertex>& vertices =
            slot.desc.depthMode == DebugLineDepthMode::DepthTested ? result.depthTested : result.overlay;
        vertices.push_back({slot.desc.start, slot.desc.color});
        vertices.push_back({slot.desc.end, slot.desc.color});
    }
    return result;
}

DebugLineHandle DebugLineRegistry::MakeHandle(uint32_t index, uint32_t generation)
{
    return ((generation & kGenerationMask) << kIndexBits) | (index + 1u);
}

bool DebugLineRegistry::DecodeHandle(DebugLineHandle handle, uint32_t& index, uint32_t& generation)
{
    const uint32_t encodedIndex = handle & kIndexMask;
    if (encodedIndex == 0)
    {
        return false;
    }
    index = encodedIndex - 1u;
    generation = handle >> kIndexBits;
    return index < kMaxHostDebugLines && generation != 0;
}

DebugLineRegistry::Slot* DebugLineRegistry::Find(DebugLineHandle handle)
{
    uint32_t index = 0;
    uint32_t generation = 0;
    if (!DecodeHandle(handle, index, generation))
    {
        return nullptr;
    }
    Slot& slot = m_slots[index];
    return slot.occupied && slot.generation == generation ? &slot : nullptr;
}

} // namespace RtPbrSurvey
