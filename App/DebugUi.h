#pragma once

#include "../Engine/RtPbrSurveyEngine.h"

class RtPbrSurveyApp;

namespace App
{

void DrawWireframeOverlay(const RtPbrSurveyEngine& engine, bool showControls);
void DrawDebugUi(RtPbrSurveyApp& app, const RtPbrSurveyEngine::UiFrameContext& context);

} // namespace App
