#pragma once

class RtPbrSurveyApp;

namespace App
{

enum class EvaluationCaseScope
{
    AllScenes,
    CurrentScene,
};

void DrawEvaluationCasesWindow(RtPbrSurveyApp& app, EvaluationCaseScope scope);

} // namespace App
