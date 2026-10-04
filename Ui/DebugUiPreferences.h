#pragma once

namespace RtPbrSurvey
{

struct DebugUiPreferences
{
    bool dlssSrDetailed = true;
    bool dlssRrDetailed = true;
    bool hybridReflectionDetailed = false;
    bool informationWindowVisible = false;
    bool evaluationCommentsVisible = true;
};

DebugUiPreferences& GetDebugUiPreferences();
void RegisterDebugUiPreferencesSettingsHandler();
void MarkDebugUiPreferencesDirty();

} // namespace RtPbrSurvey
