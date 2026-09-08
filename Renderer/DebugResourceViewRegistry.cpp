#include "stdafx.h"

#include "DebugResourceViewRegistry.h"

#include <utility>

namespace Engine
{
void DebugResourceViewRegistry::Register(DebugResourceViewDescriptor descriptor)
{
    m_unsupportedReasons.erase(descriptor.resourceName);
    m_descriptors[descriptor.resourceName] = std::move(descriptor);
}

void DebugResourceViewRegistry::RegisterUnsupported(std::string resourceName, std::string reason)
{
    m_descriptors.erase(resourceName);
    m_unsupportedReasons[std::move(resourceName)] = std::move(reason);
}

DebugResourceInspection DebugResourceViewRegistry::Inspect(const std::string& resourceName) const
{
    const auto descriptor = m_descriptors.find(resourceName);
    if (descriptor != m_descriptors.end())
    {
        return {&descriptor->second, {}};
    }

    const auto reason = m_unsupportedReasons.find(resourceName);
    if (reason != m_unsupportedReasons.end())
    {
        return {nullptr, reason->second};
    }
    return {nullptr, "No compatible inspector is registered for this resource."};
}
} // namespace Engine
