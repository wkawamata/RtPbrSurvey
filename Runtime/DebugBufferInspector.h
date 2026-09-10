#pragma once

#include "Engine/FrameGraph/RenderGraphDocument.h"
#include "Renderer/DebugResourceViewRegistry.h"

#include <cstdint>
#include <string>

namespace RtPbrSurvey
{
struct DebugBufferInspectorModel
{
    bool isBuffer = false;
    bool schemaRegistered = false;
    bool imageLayoutRegistered = false;
    std::string resourceName;
    std::string schemaStatus;
    Engine::DebugResourceViewKind viewKind = Engine::DebugResourceViewKind::RawBuffer;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    uint32_t elementCount = 0;
    uint32_t elementStride = 0;
    uint64_t estimatedByteSize = 0;
    Engine::DebugBufferImageLayout imageLayout;
};

DebugBufferInspectorModel BuildDebugBufferInspectorModel(const Engine::RenderGraphDocumentNode& node,
                                                         const Engine::DebugResourceViewRegistry* registry);

const char* DebugResourceViewKindLabel(Engine::DebugResourceViewKind kind);
const char* DebugBufferComponentTypeLabel(Engine::DebugBufferComponentType type);
} // namespace RtPbrSurvey
