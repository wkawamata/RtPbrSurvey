#pragma once

namespace RtPbrSurvey
{

inline constexpr int kHdrImGuiWhiteMinNits = 40;
inline constexpr int kHdrImGuiWhiteMaxNits = 400;
inline constexpr int kHdrImGuiWhiteDefaultNits = 300;

struct DebugUiPreferences
{
    bool dlssSrDetailed = true;
    bool dlssRrDetailed = true;
    bool hybridReflectionDetailed = false;
    bool informationWindowVisible = false;
    bool evaluationCommentsVisible = true;
    int hdrImGuiWhiteNits = kHdrImGuiWhiteDefaultNits;
};

DebugUiPreferences& GetDebugUiPreferences();
void RegisterDebugUiPreferencesSettingsHandler();
void MarkDebugUiPreferencesDirty();

} // namespace RtPbrSurvey
