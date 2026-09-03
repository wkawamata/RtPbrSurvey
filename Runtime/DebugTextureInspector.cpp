#include "stdafx.h"

#include "Runtime/DebugTextureInspector.h"

#include <algorithm>
#include <utility>

namespace RtPbrSurvey
{
DebugTextureInspector& DebugTextureInspectorManager::OpenPreview(
    std::string resourceName, std::string displayName, DebugTextureSemantic semantic)
{
    const auto existing = std::find_if(m_inspectors.begin(), m_inspectors.end(), [](const auto& inspector) {
        return inspector.open && !inspector.pinned;
    });
    if (existing != m_inspectors.end())
    {
        existing->resourceName = std::move(resourceName);
        existing->displayName = std::move(displayName);
        existing->semantic = semantic;
        return *existing;
    }

    return Create(std::move(resourceName), std::move(displayName), semantic, false);
}

DebugTextureInspector& DebugTextureInspectorManager::PinPreview(
    std::string resourceName, std::string displayName, DebugTextureSemantic semantic)
{
    return Create(std::move(resourceName), std::move(displayName), semantic, true);
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

DebugTextureInspector& DebugTextureInspectorManager::Create(
    std::string resourceName, std::string displayName, DebugTextureSemantic semantic, bool pinned)
{
    DebugTextureInspector inspector;
    inspector.id = m_nextId++;
    inspector.resourceName = std::move(resourceName);
    inspector.displayName = std::move(displayName);
    inspector.semantic = semantic;
    inspector.pinned = pinned;
    m_inspectors.push_back(std::move(inspector));
    return m_inspectors.back();
}
} // namespace RtPbrSurvey
