#include "stdafx.h"

#include "DebugBufferInspector.h"

namespace RtPbrSurvey
{
DebugBufferInspectorModel BuildDebugBufferInspectorModel(const Engine::RenderGraphDocumentNode& node,
                                                         const Engine::DebugResourceViewRegistry* registry)
{
    DebugBufferInspectorModel model;
    model.resourceName = node.name;
    model.isBuffer = node.kind == Engine::RenderGraphNodeKind::Resource &&
                     node.resourceKind == Engine::RenderGraphResourceKind::Buffer;
    if (!model.isBuffer)
    {
        model.schemaStatus = "The selected RenderGraph resource is not a Buffer.";
        return model;
    }
    if (registry == nullptr)
    {
        model.schemaStatus = "Resource inspection registry is unavailable.";
        return model;
    }

    const Engine::DebugResourceInspection inspection = registry->Inspect(node.name);
    if (!inspection.IsInspectable())
    {
        model.schemaStatus = inspection.unsupportedReason;
        return model;
    }
    if (inspection.descriptor->viewKind == Engine::DebugResourceViewKind::Texture)
    {
        model.schemaStatus = "The registered view describes a Texture, not a Buffer schema.";
        return model;
    }

    model.schemaRegistered = true;
    model.viewKind = inspection.descriptor->viewKind;
    model.format = inspection.descriptor->format;
    model.elementCount = inspection.descriptor->elementCount;
    model.elementStride = inspection.descriptor->elementStride;
    model.estimatedByteSize = static_cast<uint64_t>(model.elementCount) * model.elementStride;
    model.imageLayout = inspection.descriptor->imageLayout;
    model.imageLayoutRegistered = model.imageLayout.IsValid(model.elementCount, model.elementStride);
    model.schemaStatus = model.imageLayoutRegistered
                             ? "Registered schema includes a valid 2D image layout."
                             : "Registered schema is metadata-only; no valid 2D image layout is available.";
    return model;
}

const char* DebugResourceViewKindLabel(Engine::DebugResourceViewKind kind)
{
    switch (kind)
    {
        case Engine::DebugResourceViewKind::Texture:
            return "Texture";
        case Engine::DebugResourceViewKind::StructuredBuffer:
            return "Structured Buffer";
        case Engine::DebugResourceViewKind::RawBuffer:
            return "Raw Buffer";
        case Engine::DebugResourceViewKind::ScalarSeries:
            return "Scalar Series";
        case Engine::DebugResourceViewKind::VectorSeries:
            return "Vector Series";
        case Engine::DebugResourceViewKind::Counter:
            return "Counter";
        default:
            return "Unknown";
    }
}

const char* DebugBufferComponentTypeLabel(Engine::DebugBufferComponentType type)
{
    switch (type)
    {
        case Engine::DebugBufferComponentType::Float32:
            return "Float32";
        case Engine::DebugBufferComponentType::Uint32:
            return "Uint32";
        case Engine::DebugBufferComponentType::Sint32:
            return "Sint32";
        default:
            return "Unknown";
    }
}
} // namespace RtPbrSurvey
