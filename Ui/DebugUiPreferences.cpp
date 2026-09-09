#include "stdafx.h"

#include "Ui/DebugUiPreferences.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <cstdio>
#include <cstring>

namespace RtPbrSurvey
{

namespace
{
DebugUiPreferences g_preferences;

void* ReadOpen(ImGuiContext*, ImGuiSettingsHandler*, const char* name)
{
    return std::strcmp(name, "DebugUi") == 0 ? &g_preferences : nullptr;
}

void ReadLine(ImGuiContext*, ImGuiSettingsHandler*, void* entry, const char* line)
{
    auto& preferences = *static_cast<DebugUiPreferences*>(entry);
    int value = 0;
    if (sscanf_s(line, "DlssDetailed=%d", &value) == 1)
    {
        preferences.dlssSrDetailed = value != 0;
        preferences.dlssRrDetailed = value != 0;
    }
    else if (sscanf_s(line, "DlssSrDetailed=%d", &value) == 1)
    {
        preferences.dlssSrDetailed = value != 0;
    }
    else if (sscanf_s(line, "DlssRrDetailed=%d", &value) == 1)
    {
        preferences.dlssRrDetailed = value != 0;
    }
    else if (sscanf_s(line, "HybridReflectionDetailed=%d", &value) == 1)
    {
        preferences.hybridReflectionDetailed = value != 0;
    }
    else if (sscanf_s(line, "InformationWindowVisible=%d", &value) == 1)
    {
        preferences.informationWindowVisible = value != 0;
    }
}

void WriteAll(ImGuiContext*, ImGuiSettingsHandler* handler, ImGuiTextBuffer* output)
{
    output->appendf("[%s][DebugUi]\n", handler->TypeName);
    output->appendf("DlssSrDetailed=%d\n", g_preferences.dlssSrDetailed ? 1 : 0);
    output->appendf("DlssRrDetailed=%d\n", g_preferences.dlssRrDetailed ? 1 : 0);
    output->appendf("HybridReflectionDetailed=%d\n", g_preferences.hybridReflectionDetailed ? 1 : 0);
    output->appendf("InformationWindowVisible=%d\n\n", g_preferences.informationWindowVisible ? 1 : 0);
}
} // namespace

DebugUiPreferences& GetDebugUiPreferences()
{
    return g_preferences;
}

void RegisterDebugUiPreferencesSettingsHandler()
{
    ImGuiContext* context = ImGui::GetCurrentContext();
    if (context == nullptr)
    {
        return;
    }

    ImGuiSettingsHandler handler;
    handler.TypeName = "RtPbrSurvey";
    handler.TypeHash = ImHashStr(handler.TypeName);
    handler.ReadOpenFn = ReadOpen;
    handler.ReadLineFn = ReadLine;
    handler.WriteAllFn = WriteAll;
    context->SettingsHandlers.push_back(handler);
}

void MarkDebugUiPreferencesDirty()
{
    ImGui::MarkIniSettingsDirty();
}

} // namespace RtPbrSurvey
