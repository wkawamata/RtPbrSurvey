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

bool TestOpenPreviewReusesUnpinnedInspector()
{
    RtPbrSurvey::DebugTextureInspectorManager manager;
    const uint64_t firstId = manager.OpenPreview("GBuffer.Albedo", "Albedo", RtPbrSurvey::DebugTextureSemantic::Color).id;
    const auto& second = manager.OpenPreview("GBuffer.Depth", "Depth", RtPbrSurvey::DebugTextureSemantic::Depth);

    return Check(manager.Inspectors().size() == 1, "OpenPreview reuses the transient inspector") &&
           Check(second.id == firstId, "reused inspector keeps its stable id") &&
           Check(second.resourceName == "GBuffer.Depth", "reused inspector resolves the new resource") &&
           Check(second.semantic == RtPbrSurvey::DebugTextureSemantic::Depth, "reused inspector updates semantic");
}

bool TestPinnedPreviewsRemainIndependent()
{
    RtPbrSurvey::DebugTextureInspectorManager manager;
    const auto firstId = manager.PinPreview("RR.Albedo", "Albedo", RtPbrSurvey::DebugTextureSemantic::Color).id;
    const auto secondId = manager.PinPreview("RR.Roughness", "Roughness", RtPbrSurvey::DebugTextureSemantic::Scalar).id;

    return Check(firstId != secondId, "pinned inspectors have unique ids") &&
           Check(manager.Inspectors().size() == 2, "PinPreview creates independent inspectors") &&
           Check(manager.Find(firstId)->resourceName == "RR.Albedo", "first pinned resource remains unchanged");
}

bool TestCloseAndRemove()
{
    RtPbrSurvey::DebugTextureInspectorManager manager;
    const auto id = manager.OpenPreview("LightPass.RenderTarget", "Scene Color", RtPbrSurvey::DebugTextureSemantic::Color).id;

    bool passed = Check(manager.Close(id), "existing inspector closes");
    passed &= Check(!manager.Close(999), "unknown inspector does not close");
    manager.RemoveClosed();
    passed &= Check(manager.Inspectors().empty(), "closed inspectors are removed");
    return passed;
}
} // namespace

int main()
{
    const bool passed = TestOpenPreviewReusesUnpinnedInspector() && TestPinnedPreviewsRemainIndependent() &&
                        TestCloseAndRemove();
    if (passed)
    {
        std::cout << "DebugTextureInspector tests passed.\n";
        return 0;
    }

    return 1;
}
