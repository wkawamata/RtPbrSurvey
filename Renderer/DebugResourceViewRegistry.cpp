#include "stdafx.h"

#include "DebugResourceViewRegistry.h"

#include <utility>

namespace Engine
{
bool DebugBufferImageLayout::IsValid(uint32_t elementCount, uint32_t elementStride) const
{
    if (width == 0 || height == 0 || elementCount == 0 || elementStride == 0 || componentCount == 0 ||
        componentCount > 4 || componentType == DebugBufferComponentType::Unknown)
    {
        return false;
    }

    const uint64_t rowStride = rowStrideElements != 0 ? rowStrideElements : width;
    const uint64_t requiredElementCount = (static_cast<uint64_t>(height) - 1) * rowStride + width;
    const uint64_t requiredComponentBytes =
        static_cast<uint64_t>(componentOffsetBytes) + static_cast<uint64_t>(componentCount) * sizeof(uint32_t);
    return rowStride >= width && requiredElementCount <= elementCount && requiredComponentBytes <= elementStride;
}

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
