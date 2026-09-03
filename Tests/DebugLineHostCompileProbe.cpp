#include "Runtime/SceneRenderer.h"

#include <type_traits>

static_assert(std::is_same_v<RtPbrSurvey::DebugLineHandle, uint32_t>);
static_assert(
    std::is_same_v<decltype(&RtPbrSurvey::SceneRenderer::AddDebugLine),
                   RtPbrSurvey::DebugLineHandle (RtPbrSurvey::SceneRenderer::*)(const RtPbrSurvey::DebugLineDesc&)>);

int main()
{
    RtPbrSurvey::DebugLineDesc desc;
    desc.depthMode = RtPbrSurvey::DebugLineDepthMode::Overlay;
    return desc.visible ? 0 : 1;
}
