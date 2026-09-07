#include "stdafx.h"

#include "Runtime/DebugTextureInspector.h"

#include <iostream>

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

bool TestPreviewLimit()
{
    RtPbrSurvey::DebugTextureInspectorManager manager;
    for (size_t i = 0; i < RtPbrSurvey::DebugTextureInspectorManager::kMaxInspectorCount; ++i)
    {
        if (manager.OpenPreview("Resource." + std::to_string(i), "Resource", RtPbrSurvey::DebugTextureSemantic::Color) ==
            nullptr)
        {
            return Check(false, "preview opens below the limit");
        }
    }

    return Check(manager.OpenPreview("Resource.overflow", "Overflow", RtPbrSurvey::DebugTextureSemantic::Color) ==
                     nullptr,
                 "preview beyond the limit is rejected");
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
} // namespace

int main()
{
    const bool passed = TestOpenPreviewReusesMatchingInspector() && TestPinnedPreviewsRemainIndependent() &&
                        TestDifferentPreviewsRemainIndependent() && TestPreviewLimit() && TestCloseAndRemove() &&
                        TestClosedSlotIsReusedWithoutMovingLiveInspectors();
    if (passed)
    {
        std::cout << "DebugTextureInspector tests passed.\n";
        return 0;
    }

    return 1;
}
