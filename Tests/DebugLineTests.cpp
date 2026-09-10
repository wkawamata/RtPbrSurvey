#include "Runtime/DebugLine.h"

#include <iostream>
#include <stdexcept>

namespace
{

void Require(bool condition, const char* message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

RtPbrSurvey::DebugLineDesc MakeLine(float x, RtPbrSurvey::DebugLineDepthMode depthMode)
{
    RtPbrSurvey::DebugLineDesc desc;
    desc.start = {x, 1.0f, 2.0f};
    desc.end = {x + 1.0f, 3.0f, 4.0f};
    desc.color = {0.25f, 0.5f, 0.75f, 1.0f};
    desc.depthMode = depthMode;
    return desc;
}

void TestLifecycleAndStaleHandles()
{
    RtPbrSurvey::DebugLineRegistry registry;
    const RtPbrSurvey::DebugLineHandle first =
        registry.Add(MakeLine(0.0f, RtPbrSurvey::DebugLineDepthMode::DepthTested));
    Require(first != RtPbrSurvey::kInvalidDebugLineHandle, "Add should return a valid handle.");

    RtPbrSurvey::DebugLineDesc updated = MakeLine(5.0f, RtPbrSurvey::DebugLineDepthMode::Overlay);
    Require(registry.Update(first, updated), "A live handle should update.");
    RtPbrSurvey::DebugLineVertexGroups vertices = registry.AssembleVertices();
    Require(vertices.depthTested.empty() && vertices.overlay.size() == 2, "Updated lines should change groups.");
    Require(vertices.overlay[0].position.x == 5.0f, "Updated geometry should be assembled.");

    updated.visible = false;
    Require(registry.Update(first, updated), "A line should be hideable.");
    vertices = registry.AssembleVertices();
    Require(vertices.depthTested.empty() && vertices.overlay.empty(), "Hidden lines should not emit vertices.");

    registry.Remove(first);
    Require(!registry.Update(first, updated), "A removed handle should become stale.");
    registry.Remove(first);
    const RtPbrSurvey::DebugLineHandle replacement =
        registry.Add(MakeLine(7.0f, RtPbrSurvey::DebugLineDepthMode::DepthTested));
    Require(replacement != first, "A reused slot should have a new generation.");
    Require(!registry.Update(first, updated), "A stale generation must not update a reused slot.");
    Require(!registry.Update(RtPbrSurvey::kInvalidDebugLineHandle, updated), "Handle zero should be invalid.");
    registry.Remove(RtPbrSurvey::kInvalidDebugLineHandle);
}

void TestCapacityClearAndClassification()
{
    RtPbrSurvey::DebugLineRegistry registry;
    for (uint32_t index = 0; index < RtPbrSurvey::kMaxHostDebugLines; index++)
    {
        const RtPbrSurvey::DebugLineDepthMode mode =
            index % 2 == 0 ? RtPbrSurvey::DebugLineDepthMode::DepthTested : RtPbrSurvey::DebugLineDepthMode::Overlay;
        Require(registry.Add(MakeLine(static_cast<float>(index), mode)) != RtPbrSurvey::kInvalidDebugLineHandle,
                "Every slot up to capacity should be available.");
    }
    Require(registry.Add(MakeLine(0.0f, RtPbrSurvey::DebugLineDepthMode::Overlay)) ==
                RtPbrSurvey::kInvalidDebugLineHandle,
            "Adding beyond capacity should return the invalid handle.");

    const RtPbrSurvey::DebugLineVertexGroups vertices = registry.AssembleVertices();
    Require(vertices.depthTested.size() == RtPbrSurvey::kMaxHostDebugLines,
            "Depth-tested lines should emit two vertices each.");
    Require(vertices.overlay.size() == RtPbrSurvey::kMaxHostDebugLines, "Overlay lines should emit two vertices each.");
    registry.Clear();
    Require(registry.AssembleVertices().depthTested.empty() && registry.AssembleVertices().overlay.empty(),
            "Clear should remove all lines.");
}

} // namespace

int main()
{
    try
    {
        TestLifecycleAndStaleHandles();
        TestCapacityClearAndClassification();
    }
    catch (const std::exception& error)
    {
        std::cerr << "Debug line tests failed: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
