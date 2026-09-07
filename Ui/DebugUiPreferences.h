#pragma once

namespace RtPbrSurvey
{

struct DebugUiPreferences
{
    bool dlssDetailed = true;
    bool hybridReflectionDetailed = false;
};

DebugUiPreferences& GetDebugUiPreferences();
void RegisterDebugUiPreferencesSettingsHandler();
void MarkDebugUiPreferencesDirty();

} // namespace RtPbrSurvey
