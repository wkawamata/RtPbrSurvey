#pragma once

namespace RtPbrSurvey
{

struct DebugUiPreferences
{
    bool dlssSrDetailed = true;
    bool dlssRrDetailed = true;
    bool hybridReflectionDetailed = false;
    bool informationWindowVisible = false;
};

DebugUiPreferences& GetDebugUiPreferences();
void RegisterDebugUiPreferencesSettingsHandler();
void MarkDebugUiPreferencesDirty();

} // namespace RtPbrSurvey
