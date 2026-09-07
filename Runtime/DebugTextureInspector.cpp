#include "stdafx.h"

#include "Runtime/DebugTextureInspector.h"

#include <algorithm>
#include <utility>

namespace RtPbrSurvey
{
DebugTextureInspector* DebugTextureInspectorManager::OpenPreview(
    std::string resourceName, std::string displayName, DebugTextureSemantic semantic)
{
    return Open(std::move(resourceName), std::move(displayName), semantic, false);
}

DebugTextureInspector* DebugTextureInspectorManager::PinPreview(
    std::string resourceName, std::string displayName, DebugTextureSemantic semantic)
{
    return Open(std::move(resourceName), std::move(displayName), semantic, true);
}

bool DebugTextureInspectorManager::Close(uint64_t id)
{
    DebugTextureInspector* inspector = Find(id);
    if (inspector == nullptr)
    {
        return false;
    }

    inspector->open = false;
    return true;
}

void DebugTextureInspectorManager::CloseAll()
{
    for (DebugTextureInspector& inspector : m_inspectors)
    {
        inspector.open = false;
    }
}

void DebugTextureInspectorManager::RemoveClosed()
{
    std::erase_if(m_inspectors, [](const auto& inspector) { return !inspector.open; });
}

DebugTextureInspector* DebugTextureInspectorManager::Find(uint64_t id)
{
    const auto inspector = std::find_if(
        m_inspectors.begin(), m_inspectors.end(), [id](const auto& candidate) { return candidate.id == id; });
    return inspector == m_inspectors.end() ? nullptr : &*inspector;
}

const DebugTextureInspector* DebugTextureInspectorManager::Find(uint64_t id) const
{
    const auto inspector = std::find_if(
        m_inspectors.begin(), m_inspectors.end(), [id](const auto& candidate) { return candidate.id == id; });
    return inspector == m_inspectors.end() ? nullptr : &*inspector;
}

DebugTextureInspector* DebugTextureInspectorManager::Open(
    std::string resourceName, std::string displayName, DebugTextureSemantic semantic, bool pinned)
{
    const auto existing = std::find_if(m_inspectors.begin(),
                                       m_inspectors.end(),
                                       [&resourceName](const auto& inspector)
                                       { return inspector.open && inspector.resourceName == resourceName; });
    if (existing != m_inspectors.end())
    {
        existing->displayName = std::move(displayName);
        existing->semantic = semantic;
        existing->pinned = existing->pinned || pinned;
        existing->focusRequested = true;
        return &*existing;
    }

    const size_t openInspectorCount = static_cast<size_t>(std::count_if(
        m_inspectors.begin(), m_inspectors.end(), [](const auto& inspector) { return inspector.open; }));
    if (openInspectorCount >= kMaxInspectorCount)
    {
        return nullptr;
    }

    return Create(std::move(resourceName), std::move(displayName), semantic, pinned);
}

DebugTextureInspector* DebugTextureInspectorManager::Create(
    std::string resourceName, std::string displayName, DebugTextureSemantic semantic, bool pinned)
{
    bool usedSlots[kMaxInspectorCount] = {};
    for (const DebugTextureInspector& existing : m_inspectors)
    {
        if (existing.open && existing.slotIndex < kMaxInspectorCount)
        {
            usedSlots[existing.slotIndex] = true;
        }
    }

    uint32_t slotIndex = 0;
    while (slotIndex < kMaxInspectorCount && usedSlots[slotIndex])
    {
        ++slotIndex;
    }
    if (slotIndex >= kMaxInspectorCount)
    {
        return nullptr;
    }

    DebugTextureInspector inspector;
    inspector.id = m_nextId++;
    inspector.slotIndex = slotIndex;
    inspector.resourceName = std::move(resourceName);
    inspector.displayName = std::move(displayName);
    inspector.semantic = semantic;
    inspector.pinned = pinned;
    m_inspectors.push_back(std::move(inspector));
    return &m_inspectors.back();
}
} // namespace RtPbrSurvey
