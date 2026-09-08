#pragma once

#include "DebugTexturePreviewPass.h"

#include <dxgiformat.h>

#include <cstdint>
#include <string>
#include <unordered_map>

namespace Engine
{
enum class DebugResourceViewKind
{
    Texture,
    StructuredBuffer,
    RawBuffer,
    ScalarSeries,
    VectorSeries,
    Counter,
};

enum class DebugBufferComponentType
{
    Unknown,
    Float32,
    Uint32,
    Sint32,
};

struct DebugBufferImageLayout
{
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t rowStrideElements = 0;
    uint32_t componentOffsetBytes = 0;
    uint32_t componentCount = 0;
    DebugBufferComponentType componentType = DebugBufferComponentType::Unknown;

    bool IsValid(uint32_t elementCount, uint32_t elementStride) const;
};

struct DebugResourceViewDescriptor
{
    std::string resourceName;
    DebugResourceViewKind viewKind = DebugResourceViewKind::Texture;
    DebugTexturePreviewSemantic semantic = DebugTexturePreviewSemantic::Color;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    uint32_t elementCount = 0;
    uint32_t elementStride = 0;
    DebugBufferImageLayout imageLayout;
    std::string sourceDescriptorName;
};

struct DebugResourceInspection
{
    const DebugResourceViewDescriptor* descriptor = nullptr;
    std::string unsupportedReason;

    bool IsInspectable() const
    {
        return descriptor != nullptr;
    }
};

class DebugResourceViewRegistry
{
public:
    void Register(DebugResourceViewDescriptor descriptor);
    void RegisterUnsupported(std::string resourceName, std::string reason);
    DebugResourceInspection Inspect(const std::string& resourceName) const;

private:
    std::unordered_map<std::string, DebugResourceViewDescriptor> m_descriptors;
    std::unordered_map<std::string, std::string> m_unsupportedReasons;
};
} // namespace Engine
