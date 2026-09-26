#include "stdafx.h"

#include "Runtime/SceneRendererDebugUi.h"

namespace
{
using DrawContentsSignature = void (*)(RtPbrSurvey::SceneRenderer&, RtPbrSurvey::EnvironmentMappingUiState*);
using DrawAuxiliaryWindowsSignature = void (*)(RtPbrSurvey::SceneRenderer&);

static_assert(static_cast<DrawContentsSignature>(&RtPbrSurvey::SceneRendererDebugUi::DrawContents) != nullptr);
static_assert(static_cast<DrawAuxiliaryWindowsSignature>(&RtPbrSurvey::SceneRendererDebugUi::DrawAuxiliaryWindows) != nullptr);
}

int main()
{
    return 0;
}
