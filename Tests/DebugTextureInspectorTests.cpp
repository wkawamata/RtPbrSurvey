#include "stdafx.h"

#include "Runtime/DebugBufferInspector.h"
#include "Runtime/DebugTextureInspector.h"
#include "Runtime/DebugTextureThumbnailScheduler.h"
#include "Renderer/DebugResourceViewRegistry.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>

namespace
{
bool Check(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "FAILED: " << message << '\n';
    }
    return condition;
}

bool TestOpenPreviewReusesMatchingInspector()
{
    RtPbrSurvey::DebugTextureInspectorManager manager;
    const auto* first = manager.OpenPreview("GBuffer.Albedo", "Albedo", RtPbrSurvey::DebugTextureSemantic::Color);
    const uint64_t firstId = first != nullptr ? first->id : 0;
    const auto* second = manager.OpenPreview("GBuffer.Albedo", "Albedo 2", RtPbrSurvey::DebugTextureSemantic::Color);

    return Check(first != nullptr && second != nullptr, "matching previews open") &&
           Check(manager.Inspectors().size() == 1, "OpenPreview reuses the matching inspector") &&
           Check(second->id == firstId, "reused inspector keeps its stable id") &&
           Check(second->focusRequested, "reused inspector requests focus");
}

bool TestPinnedPreviewsRemainIndependent()
{
    RtPbrSurvey::DebugTextureInspectorManager manager;
    const auto* first = manager.PinPreview("RR.Albedo", "Albedo", RtPbrSurvey::DebugTextureSemantic::Color);
    const uint64_t firstId = first != nullptr ? first->id : 0;
    const auto* second = manager.PinPreview("RR.Roughness", "Roughness", RtPbrSurvey::DebugTextureSemantic::Scalar);
    const uint64_t secondId = second != nullptr ? second->id : 0;

    return Check(first != nullptr && second != nullptr, "pinned previews open") &&
           Check(firstId != secondId, "pinned inspectors have unique ids") &&
           Check(manager.Inspectors().size() == 2, "PinPreview creates independent inspectors") &&
           Check(manager.Find(firstId)->resourceName == "RR.Albedo", "first pinned resource remains unchanged");
}

bool TestDifferentPreviewsRemainIndependent()
{
    RtPbrSurvey::DebugTextureInspectorManager manager;
    const auto* first = manager.OpenPreview("GBuffer.Albedo", "Albedo", RtPbrSurvey::DebugTextureSemantic::Color);
    const uint64_t firstId = first != nullptr ? first->id : 0;
    const auto* second = manager.OpenPreview("GBuffer.Depth", "Depth", RtPbrSurvey::DebugTextureSemantic::Depth);

    return Check(first != nullptr && second != nullptr, "different previews open") &&
           Check(manager.Inspectors().size() == 2, "different resources create independent inspectors") &&
           Check(second->id != firstId, "different resources receive unique ids") &&
           Check(manager.Find(firstId)->resourceName == "GBuffer.Albedo", "first preview remains unchanged");
}

bool TestPreviewLimitUsesFifoReplacement()
{
    RtPbrSurvey::DebugTextureInspectorManager manager;
    bool passed = Check(RtPbrSurvey::DebugTextureInspectorManager::kMaxInspectorCount == 8,
                        "preview limit is increased to eight");
    for (size_t i = 0; i < RtPbrSurvey::DebugTextureInspectorManager::kMaxInspectorCount; ++i)
    {
        if (manager.OpenPreview(
                "Resource." + std::to_string(i), "Resource", RtPbrSurvey::DebugTextureSemantic::Color) == nullptr)
        {
            return Check(false, "preview opens below the limit");
        }
    }

    const uint64_t oldestId = manager.Inspectors().front().id;
    const uint32_t oldestSlot = manager.Inspectors().front().slotIndex;
    const auto* replacement =
        manager.OpenPreview("Resource.overflow", "Overflow", RtPbrSurvey::DebugTextureSemantic::Color);
    passed &= Check(replacement != nullptr, "preview beyond the limit replaces the FIFO entry");
    passed &= Check(manager.Inspectors().size() == RtPbrSurvey::DebugTextureInspectorManager::kMaxInspectorCount,
                    "FIFO replacement keeps the preview count bounded");
    passed &= Check(manager.Find(oldestId) == nullptr, "oldest unpinned preview is removed");
    passed &= Check(replacement != nullptr && replacement->slotIndex == oldestSlot,
                    "FIFO replacement reuses the evicted GPU slot");
    return passed;
}

bool TestFifoReplacementSkipsPinnedPreviews()
{
    RtPbrSurvey::DebugTextureInspectorManager manager;
    const auto* pinned = manager.PinPreview("Resource.pinned", "Pinned", RtPbrSurvey::DebugTextureSemantic::Color);
    const uint64_t pinnedId = pinned != nullptr ? pinned->id : 0;
    for (size_t i = 1; i < RtPbrSurvey::DebugTextureInspectorManager::kMaxInspectorCount; ++i)
    {
        manager.OpenPreview(
            "Resource." + std::to_string(i), "Resource", RtPbrSurvey::DebugTextureSemantic::Color);
    }

    const auto* replacement =
        manager.OpenPreview("Resource.overflow", "Overflow", RtPbrSurvey::DebugTextureSemantic::Color);
    return Check(replacement != nullptr, "FIFO replacement opens with a pinned preview present") &&
           Check(manager.Find(pinnedId) != nullptr, "pinned preview is not evicted") &&
           Check(std::none_of(manager.Inspectors().begin(),
                              manager.Inspectors().end(),
                              [](const auto& inspector) { return inspector.resourceName == "Resource.1"; }),
                 "oldest unpinned preview is evicted first");
}

bool TestAllPinnedPreviewsRejectReplacement()
{
    RtPbrSurvey::DebugTextureInspectorManager manager;
    for (size_t i = 0; i < RtPbrSurvey::DebugTextureInspectorManager::kMaxInspectorCount; ++i)
    {
        manager.PinPreview("Resource." + std::to_string(i), "Pinned", RtPbrSurvey::DebugTextureSemantic::Color);
    }

    return Check(manager.OpenPreview("Resource.overflow", "Overflow", RtPbrSurvey::DebugTextureSemantic::Color) ==
                     nullptr,
                 "all pinned previews reject FIFO replacement") &&
           Check(manager.Inspectors().size() == RtPbrSurvey::DebugTextureInspectorManager::kMaxInspectorCount,
                 "rejected replacement leaves pinned previews unchanged");
}

bool TestCloseAndRemove()
{
    RtPbrSurvey::DebugTextureInspectorManager manager;
    const auto* inspector =
        manager.OpenPreview("LightPass.RenderTarget", "Scene Color", RtPbrSurvey::DebugTextureSemantic::Color);
    const auto id = inspector->id;

    bool passed = Check(manager.Close(id), "existing inspector closes");
    passed &= Check(!manager.Close(999), "unknown inspector does not close");
    manager.RemoveClosed();
    passed &= Check(manager.Inspectors().empty(), "closed inspectors are removed");
    return passed;
}

bool TestClosedSlotIsReusedWithoutMovingLiveInspectors()
{
    RtPbrSurvey::DebugTextureInspectorManager manager;
    const auto* first = manager.OpenPreview("Resource.0", "First", RtPbrSurvey::DebugTextureSemantic::Color);
    const uint64_t firstId = first != nullptr ? first->id : 0;
    const auto* second = manager.OpenPreview("Resource.1", "Second", RtPbrSurvey::DebugTextureSemantic::Color);
    const uint64_t secondId = second != nullptr ? second->id : 0;
    const uint32_t secondSlot = second != nullptr ? second->slotIndex : UINT32_MAX;
    manager.Close(firstId);
    manager.RemoveClosed();
    const auto* replacement =
        manager.OpenPreview("Resource.2", "Replacement", RtPbrSurvey::DebugTextureSemantic::Color);

    return Check(replacement != nullptr, "replacement preview opens") &&
           Check(replacement->slotIndex == 0, "lowest closed slot is reused") &&
           Check(manager.Find(secondId)->slotIndex == secondSlot, "live inspector keeps its GPU slot");
}

bool TestDepthVisualizationDefaultsFollowCameraRange()
{
    const Engine::DepthVisualizationSettings longRange =
        Engine::DepthVisualizationSettings::CreateDefault(0.25f, 1000.0f);
    const Engine::DepthVisualizationSettings shortRange =
        Engine::DepthVisualizationSettings::CreateDefault(0.5f, 25.0f);

    return Check(longRange.mode == Engine::DepthVisualizationMode::LogView, "depth defaults to Log View") &&
           Check(longRange.displayNear == 0.25f, "depth default near follows the camera") &&
           Check(longRange.displayFar == 100.0f, "large camera range receives a bounded display far") &&
           Check(shortRange.displayFar == 25.0f, "short camera range keeps the camera far plane");
}

bool TestDepthVisualizationConstantsSanitizeInvalidValues()
{
    Engine::DepthVisualizationSettings settings;
    settings.displayNear = std::numeric_limits<float>::quiet_NaN();
    settings.displayFar = -1.0f;
    settings.gamma = 0.0f;
    const Engine::DepthVisualizationShaderConstants constants =
        settings.MakeShaderConstants(0.0f, std::numeric_limits<float>::infinity(), true);

    return Check(std::isfinite(constants.displayNear) && constants.displayNear > 0.0f,
                 "invalid display near is sanitized") &&
           Check(constants.displayFar > constants.displayNear, "display far remains greater than display near") &&
           Check(constants.gamma == 1.0f, "invalid gamma falls back to one") &&
           Check(constants.cameraNear == 0.1f && constants.cameraFar == 100.0f,
                 "invalid camera range uses stable defaults") &&
           Check(constants.orthographicProjection == 1u, "projection kind reaches shader constants");
}

bool TestDebugResourceViewRegistryReportsSupportAndReasons()
{
    Engine::DebugResourceViewRegistry registry;
    registry.Register({"DepthStencil",
                       Engine::DebugResourceViewKind::Texture,
                       Engine::DebugTexturePreviewSemantic::Depth,
                       DXGI_FORMAT_R32_FLOAT});
    registry.RegisterUnsupported("BackBuffer", "No Preview SRV.");

    const Engine::DebugResourceInspection depth = registry.Inspect("DepthStencil");
    const Engine::DebugResourceInspection backBuffer = registry.Inspect("BackBuffer");
    const Engine::DebugResourceInspection unknown = registry.Inspect("Unknown.Buffer");
    return Check(depth.IsInspectable(), "registered texture is inspectable") &&
           Check(depth.descriptor->semantic == Engine::DebugTexturePreviewSemantic::Depth,
                 "registered texture keeps its semantic") &&
           Check(!backBuffer.IsInspectable() && backBuffer.unsupportedReason == "No Preview SRV.",
                 "registered unsupported reason is reported") &&
           Check(!unknown.IsInspectable() && !unknown.unsupportedReason.empty(),
                 "unregistered resources receive a fallback reason");
}

Engine::RenderGraphDocumentNode MakeBufferNode(const char* resourceName)
{
    Engine::RenderGraphDocumentNode node;
    node.kind = Engine::RenderGraphNodeKind::Resource;
    node.resourceKind = Engine::RenderGraphResourceKind::Buffer;
    node.name = resourceName;
    return node;
}

bool TestBufferInspectorDoesNotGuessUnknownSchema()
{
    Engine::DebugResourceViewRegistry registry;
    const RtPbrSurvey::DebugBufferInspectorModel model =
        RtPbrSurvey::BuildDebugBufferInspectorModel(MakeBufferNode("Unknown.Buffer"), &registry);
    return Check(model.isBuffer, "RenderGraph Buffer is accepted by the metadata inspector") &&
           Check(!model.schemaRegistered, "unknown Buffer schema is not guessed") &&
           Check(!model.imageLayoutRegistered, "unknown Buffer is not treated as an image") &&
           Check(model.estimatedByteSize == 0, "unknown Buffer size is not invented");
}

bool TestBufferInspectorValidatesRegisteredImageLayout()
{
    Engine::DebugResourceViewRegistry registry;
    Engine::DebugResourceViewDescriptor descriptor;
    descriptor.resourceName = "Denoiser.Tiles";
    descriptor.viewKind = Engine::DebugResourceViewKind::StructuredBuffer;
    descriptor.elementCount = 128 * 72;
    descriptor.elementStride = 16;
    descriptor.imageLayout = {128, 72, 128, 0, 4, Engine::DebugBufferComponentType::Float32};
    registry.Register(descriptor);

    const RtPbrSurvey::DebugBufferInspectorModel model =
        RtPbrSurvey::BuildDebugBufferInspectorModel(MakeBufferNode("Denoiser.Tiles"), &registry);
    return Check(model.schemaRegistered, "registered Buffer schema is reported") &&
           Check(model.imageLayoutRegistered, "valid 2D Buffer layout enables image conversion") &&
           Check(model.estimatedByteSize == 128ull * 72ull * 16ull, "registered Buffer byte size is calculated");
}

bool TestBufferInspectorRejectsOutOfRangeImageLayout()
{
    Engine::DebugResourceViewRegistry registry;
    Engine::DebugResourceViewDescriptor descriptor;
    descriptor.resourceName = "Short.Buffer";
    descriptor.viewKind = Engine::DebugResourceViewKind::RawBuffer;
    descriptor.elementCount = 16;
    descriptor.elementStride = 4;
    descriptor.imageLayout = {8, 8, 8, 0, 1, Engine::DebugBufferComponentType::Float32};
    registry.Register(descriptor);

    const RtPbrSurvey::DebugBufferInspectorModel model =
        RtPbrSurvey::BuildDebugBufferInspectorModel(MakeBufferNode("Short.Buffer"), &registry);
    return Check(model.schemaRegistered, "raw Buffer metadata remains inspectable") &&
           Check(!model.imageLayoutRegistered, "2D layout beyond the Buffer is rejected");
}

bool TestBufferPreviewConstantsPreserveImageLayout()
{
    Engine::DebugTexturePreviewSettings settings;
    settings.bufferVisualizationMode = Engine::DebugBufferVisualizationMode::Heatmap;
    settings.channel = Engine::DebugTexturePreviewChannel::G;
    settings.bufferWidth = 16;
    settings.bufferHeight = 8;
    settings.bufferRowStrideElements = 20;
    settings.bufferElementStride = 32;
    settings.bufferComponentOffsetBytes = 12;
    settings.bufferComponentCount = 2;
    settings.bufferComponentType = static_cast<UINT>(Engine::DebugBufferComponentType::Float32);
    const Engine::DebugTexturePreviewSettings::ShaderConstants constants =
        settings.MakeShaderConstants(0.1f, 100.0f, false);
    return Check(constants.bufferVisualizationMode == 1, "Heatmap mode reaches shader constants") &&
           Check(constants.channel == 2, "Buffer channel reaches shader constants") &&
           Check(constants.bufferWidth == 16 && constants.bufferHeight == 8,
                 "Buffer image dimensions reach shader constants") &&
           Check(constants.bufferRowStrideElements == 20 && constants.bufferElementStride == 32,
                 "Buffer strides reach shader constants") &&
           Check(constants.bufferComponentOffsetBytes == 12 && constants.bufferComponentCount == 2,
                 "Buffer component layout reaches shader constants");
}

Engine::DebugResourceViewDescriptor MakeThumbnailDescriptor(const char* resourceName)
{
    return {resourceName,
            Engine::DebugResourceViewKind::Texture,
            Engine::DebugTexturePreviewSemantic::Color,
            DXGI_FORMAT_R16G16B16A16_FLOAT};
}

bool TestThumbnailSchedulerPrioritizesSelectionAndBoundsSlots()
{
    RtPbrSurvey::DebugTextureThumbnailScheduler scheduler;
    scheduler.Request(MakeThumbnailDescriptor("Selected"), true);
    scheduler.Request(MakeThumbnailDescriptor("Visible.0"), false);
    scheduler.Request(MakeThumbnailDescriptor("Visible.1"), false);
    scheduler.Request(MakeThumbnailDescriptor("Visible.2"), false);
    scheduler.Request(MakeThumbnailDescriptor("Visible.3"), false);
    const auto firstPlan = scheduler.BuildFramePlan(1);
    const size_t activeCount = static_cast<size_t>(
        std::count_if(firstPlan.begin(), firstPlan.end(), [](const auto& slot) { return slot.active; }));
    const auto selected = std::find_if(
        firstPlan.begin(), firstPlan.end(), [](const auto& slot) { return slot.resourceName == "Selected"; });

    scheduler.Request(MakeThumbnailDescriptor("Selected"), true);
    scheduler.Request(MakeThumbnailDescriptor("Visible.0"), false);
    const auto secondPlan = scheduler.BuildFramePlan(2);
    const auto selectedSecond = std::find_if(
        secondPlan.begin(), secondPlan.end(), [](const auto& slot) { return slot.resourceName == "Selected"; });
    const bool selectedReady = scheduler.FindReadySlot("Selected").has_value();
    const bool graceRetained = scheduler.FindReadySlot("Visible.1").has_value();
    scheduler.BuildFramePlan(5);
    return Check(activeCount == RtPbrSurvey::DebugTextureThumbnailScheduler::kSlotCount,
                 "thumbnail slots remain bounded") &&
           Check(selected != firstPlan.end() && selected->update, "selected thumbnail receives a slot") &&
           Check(selectedSecond != secondPlan.end() && selectedSecond->update,
                 "selected thumbnail updates every frame") &&
           Check(selectedReady, "updated thumbnail becomes ready next frame") &&
           Check(graceRetained, "brief visibility gaps retain thumbnail slots") &&
           Check(!scheduler.FindReadySlot("Visible.1").has_value(),
                 "unrequested thumbnail retires after the visibility grace period");
}

bool TestThumbnailSchedulerKeepsVisibleResourcesStable()
{
    RtPbrSurvey::DebugTextureThumbnailScheduler scheduler;
    for (int i = 0; i < 5; ++i)
    {
        scheduler.Request(MakeThumbnailDescriptor(("Visible." + std::to_string(i)).c_str()), false);
    }
    const auto firstPlan = scheduler.BuildFramePlan(1);
    for (int i = 0; i < 5; ++i)
    {
        scheduler.Request(MakeThumbnailDescriptor(("Visible." + std::to_string(i)).c_str()), false);
    }
    const auto throttledPlan = scheduler.BuildFramePlan(2);
    for (int i = 0; i < 5; ++i)
    {
        scheduler.Request(MakeThumbnailDescriptor(("Visible." + std::to_string(i)).c_str()), false);
    }
    const auto refreshedPlan = scheduler.BuildFramePlan(9);
    const bool firstUpdated =
        std::all_of(firstPlan.begin(), firstPlan.end(), [](const auto& slot) { return slot.active && slot.update; });
    const bool secondSkipped =
        std::none_of(throttledPlan.begin(), throttledPlan.end(), [](const auto& slot) { return slot.update; });
    const bool assignmentsStable = std::equal(firstPlan.begin(),
                                              firstPlan.end(),
                                              refreshedPlan.begin(),
                                              [](const auto& first, const auto& refreshed)
                                              { return first.resourceName == refreshed.resourceName; });
    const bool refreshed =
        std::all_of(refreshedPlan.begin(), refreshedPlan.end(), [](const auto& slot) { return slot.update; });

    scheduler.Request(MakeThumbnailDescriptor("Visible.4"), false);
    const auto replacementPlan = scheduler.BuildFramePlan(10);
    const bool replacementAdded = std::any_of(replacementPlan.begin(), replacementPlan.end(), [](const auto& slot)
                                              { return slot.resourceName == "Visible.4" && slot.update; });
    return Check(firstUpdated, "new visible thumbnail slots update immediately") &&
           Check(secondSkipped, "visible thumbnails are throttled between refreshes") &&
           Check(assignmentsStable, "visible resources keep stable thumbnail slots") &&
           Check(refreshed, "stable thumbnails refresh without rotating resources") &&
           Check(replacementAdded, "newly visible resource replaces an unrequested thumbnail");
}

bool TestThumbnailSchedulerAcceptsRegisteredBufferImages()
{
    Engine::DebugResourceViewDescriptor descriptor;
    descriptor.resourceName = "Image.Buffer";
    descriptor.viewKind = Engine::DebugResourceViewKind::StructuredBuffer;
    descriptor.elementCount = 16;
    descriptor.elementStride = 16;
    descriptor.imageLayout = {4, 4, 4, 0, 4, Engine::DebugBufferComponentType::Float32};
    descriptor.sourceDescriptorName = "ImageBufferRawSrv";

    RtPbrSurvey::DebugTextureThumbnailScheduler scheduler;
    scheduler.Request(descriptor, true);
    const auto plan = scheduler.BuildFramePlan(1);
    const auto slot = std::find_if(plan.begin(), plan.end(), [](const auto& candidate)
                                   { return candidate.resourceName == "Image.Buffer"; });
    return Check(slot != plan.end(), "registered Buffer image receives a thumbnail slot") &&
           Check(slot->viewKind == Engine::DebugResourceViewKind::StructuredBuffer,
                 "Buffer thumbnail preserves its source view kind");
}
} // namespace

int main()
{
    const bool passed =
        TestOpenPreviewReusesMatchingInspector() && TestPinnedPreviewsRemainIndependent() &&
        TestDifferentPreviewsRemainIndependent() && TestPreviewLimitUsesFifoReplacement() &&
        TestFifoReplacementSkipsPinnedPreviews() && TestAllPinnedPreviewsRejectReplacement() && TestCloseAndRemove() &&
        TestClosedSlotIsReusedWithoutMovingLiveInspectors() && TestDepthVisualizationDefaultsFollowCameraRange() &&
        TestDepthVisualizationConstantsSanitizeInvalidValues() &&
        TestDebugResourceViewRegistryReportsSupportAndReasons() && TestBufferInspectorDoesNotGuessUnknownSchema() &&
        TestBufferInspectorValidatesRegisteredImageLayout() && TestBufferInspectorRejectsOutOfRangeImageLayout() &&
        TestBufferPreviewConstantsPreserveImageLayout() &&
        TestThumbnailSchedulerPrioritizesSelectionAndBoundsSlots() &&
        TestThumbnailSchedulerKeepsVisibleResourcesStable() &&
        TestThumbnailSchedulerAcceptsRegisteredBufferImages();
    if (passed)
    {
        std::cout << "DebugTextureInspector tests passed.\n";
        return 0;
    }

    return 1;
}
