#include "stdafx.h"

#include "App/EvaluationCaseUi.h"

#include "App/RtPbrSurveyApp.h"
#include "Ui/DebugUiPreferences.h"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

namespace App
{
namespace
{
const char* RenderingPathName(const RtPbrSurvey::EvaluationState& state)
{
    switch (state.sceneConfig.value("renderingPath", 1))
    {
        case 0:
            return "Forward";
        case 2:
            return "Path Tracing";
        default:
            return "Deferred";
    }
}

const char* DlssQualityName(int qualityMode)
{
    static constexpr const char* names[] = {"DLAA", "Quality", "Balanced", "Performance", "Ultra Performance"};
    static constexpr int nameCount = static_cast<int>(sizeof(names) / sizeof(names[0]));
    return qualityMode >= 0 && qualityMode < nameCount ? names[qualityMode] : "Unknown";
}

std::string DlssSrSummary(const RtPbrSurvey::EvaluationState& state)
{
    if (!state.sceneConfig.contains("temporalUpscaler") || !state.sceneConfig.at("temporalUpscaler").is_object())
    {
        return "Off";
    }
    const nlohmann::json& settings = state.sceneConfig.at("temporalUpscaler");
    if (!settings.value("enabled", false))
    {
        return "Off";
    }
    return DlssQualityName(settings.value("qualityMode", 0));
}

const char* DlssRrSummary(const RtPbrSurvey::EvaluationState& state)
{
    if (!state.sceneConfig.contains("rayReconstruction") || !state.sceneConfig.at("rayReconstruction").is_object())
    {
        return "Off";
    }
    const nlohmann::json& settings = state.sceneConfig.at("rayReconstruction");
    if (!settings.value("enabled", false))
    {
        return "Off";
    }
    return settings.value("experimentalNativeEvaluationEnabled", false) ? "Native" : "Fallback";
}

std::string ResultSummary(const RtPbrSurvey::EvaluationState& state)
{
    int scoreCount = 0;
    int scoreTotal = 0;
    int booleanCount = 0;
    int trueCount = 0;
    for (const RtPbrSurvey::EvaluationTestItem& item : state.testItems)
    {
        if (item.judgmentKind == RtPbrSurvey::EvaluationJudgmentKind::Score1To5)
        {
            ++scoreCount;
            scoreTotal += std::clamp(item.score, 1, 5);
        }
        else
        {
            ++booleanCount;
            trueCount += item.booleanValue ? 1 : 0;
        }
    }

    char text[96] = {};
    if (scoreCount > 0 && booleanCount > 0)
    {
        sprintf_s(text,
                  "%.1f/5, %d/%d true",
                  static_cast<float>(scoreTotal) / static_cast<float>(scoreCount),
                  trueCount,
                  booleanCount);
    }
    else if (scoreCount > 0)
    {
        sprintf_s(text, "%.1f/5", static_cast<float>(scoreTotal) / static_cast<float>(scoreCount));
    }
    else if (booleanCount > 0)
    {
        sprintf_s(text, "%d/%d true", trueCount, booleanCount);
    }
    else
    {
        sprintf_s(text, "No results");
    }
    return text;
}

bool IsVisibleCase(const RtPbrSurvey::EvaluationState& state,
                   EvaluationCaseScope scope,
                   const std::string& currentSceneName)
{
    return scope == EvaluationCaseScope::AllScenes || state.sceneName == currentSceneName;
}
} // namespace

void DrawEvaluationCasesWindow(RtPbrSurveyApp& app, EvaluationCaseScope scope)
{
    RtPbrSurvey::DebugUiPreferences& preferences = RtPbrSurvey::GetDebugUiPreferences();
    std::vector<RtPbrSurvey::EvaluationState>& states = app.m_evaluationStates.States();
    const std::string currentSceneName =
        scope == EvaluationCaseScope::CurrentScene ? app.LoadedScene().Name() : std::string{};
    std::vector<size_t> visibleIndices;
    for (size_t i = 0; i < states.size(); ++i)
    {
        if (IsVisibleCase(states[i], scope, currentSceneName))
        {
            visibleIndices.push_back(i);
        }
    }

    const bool selectionVisible =
        app.m_selectedEvaluationStateIndex >= 0 &&
        std::find(visibleIndices.begin(),
                  visibleIndices.end(),
                  static_cast<size_t>(app.m_selectedEvaluationStateIndex)) != visibleIndices.end();
    if (!selectionVisible)
    {
        app.m_selectedEvaluationStateIndex = visibleIndices.empty() ? -1 : static_cast<int>(visibleIndices.front());
    }

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + 390.0f, viewport->WorkPos.y + 24.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(860.0f, 520.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Evaluation Cases"))
    {
        ImGui::End();
        return;
    }

    ImGui::Text("Path: %s", app.m_evaluationStates.Path().c_str());
    if (ImGui::Checkbox("Show Comments", &preferences.evaluationCommentsVisible))
    {
        RtPbrSurvey::MarkDebugUiPreferencesDirty();
    }
    if (scope == EvaluationCaseScope::CurrentScene)
    {
        ImGui::SameLine();
        if (ImGui::Button("Save as New Case"))
        {
            RtPbrSurvey::EvaluationState state;
            state.id = app.m_evaluationStates.NextStateId();
            state.name = "Evaluation " + std::to_string(state.id);
            std::string error;
            if (app.CaptureEvaluationState(state, &error))
            {
                states.push_back(std::move(state));
                app.m_selectedEvaluationStateIndex = static_cast<int>(states.size()) - 1;
                if (app.m_evaluationStates.Save(&error))
                {
                    app.m_evaluationStatus = "Evaluation case created.";
                }
                else
                {
                    states.pop_back();
                    app.m_selectedEvaluationStateIndex = -1;
                    app.m_evaluationStatus = "Save failed: " + error;
                }
            }
            else
            {
                app.m_evaluationStatus = "Capture failed: " + error;
            }
        }
    }

    const int columnCount = preferences.evaluationCommentsVisible ? 8 : 7;
    const ImGuiTableFlags tableFlags = ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg |
                                       ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY |
                                       ImGuiTableFlags_SizingStretchProp;
    if (ImGui::BeginTable("EvaluationCaseTable", columnCount, tableFlags, ImVec2(0.0f, 190.0f)))
    {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 1.5f);
        ImGui::TableSetupColumn("Scene", ImGuiTableColumnFlags_WidthStretch, 1.3f);
        ImGui::TableSetupColumn("Rendering", ImGuiTableColumnFlags_WidthFixed, 74.0f);
        ImGui::TableSetupColumn("DLSS SR", ImGuiTableColumnFlags_WidthFixed, 92.0f);
        ImGui::TableSetupColumn("DLSS RR", ImGuiTableColumnFlags_WidthFixed, 72.0f);
        ImGui::TableSetupColumn("ROI", ImGuiTableColumnFlags_WidthFixed, 38.0f);
        ImGui::TableSetupColumn("Results", ImGuiTableColumnFlags_WidthFixed, 108.0f);
        if (preferences.evaluationCommentsVisible)
        {
            ImGui::TableSetupColumn("Comment", ImGuiTableColumnFlags_WidthStretch, 1.7f);
        }
        ImGui::TableHeadersRow();

        std::optional<size_t> restoreIndex;
        for (size_t index : visibleIndices)
        {
            const RtPbrSurvey::EvaluationState& state = states[index];
            ImGui::PushID(static_cast<int>(state.id));
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            const std::string rowLabel = state.name.empty() ? "Untitled" : state.name;
            const bool selected = app.m_selectedEvaluationStateIndex == static_cast<int>(index);
            if (ImGui::Selectable(rowLabel.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns))
            {
                app.m_selectedEvaluationStateIndex = static_cast<int>(index);
                if (scope == EvaluationCaseScope::CurrentScene)
                {
                    restoreIndex = index;
                }
            }
            const bool openFromDoubleClick = scope == EvaluationCaseScope::AllScenes && ImGui::IsItemHovered() &&
                                             ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(state.sceneName.c_str());
            ImGui::TableSetColumnIndex(2);
            ImGui::TextUnformatted(RenderingPathName(state));
            ImGui::TableSetColumnIndex(3);
            const std::string srSummary = DlssSrSummary(state);
            ImGui::TextUnformatted(srSummary.c_str());
            ImGui::TableSetColumnIndex(4);
            ImGui::TextUnformatted(DlssRrSummary(state));
            ImGui::TableSetColumnIndex(5);
            ImGui::TextUnformatted(state.roi.enabled ? "Set" : "Off");
            ImGui::TableSetColumnIndex(6);
            const std::string resultSummary = ResultSummary(state);
            ImGui::TextUnformatted(resultSummary.c_str());
            if (preferences.evaluationCommentsVisible)
            {
                ImGui::TableSetColumnIndex(7);
                ImGui::TextUnformatted(state.comment.c_str());
                if (!state.comment.empty() && ImGui::IsItemHovered())
                {
                    ImGui::BeginTooltip();
                    ImGui::PushTextWrapPos(420.0f);
                    ImGui::TextUnformatted(state.comment.c_str());
                    ImGui::PopTextWrapPos();
                    ImGui::EndTooltip();
                }
            }
            if (openFromDoubleClick)
            {
                restoreIndex = index;
            }
            ImGui::PopID();
        }
        ImGui::EndTable();

        if (restoreIndex.has_value())
        {
            std::string error;
            app.m_evaluationStatus = app.RestoreEvaluationState(states[*restoreIndex], &error)
                                         ? "Evaluation case restored."
                                         : "Restore failed: " + error;
        }
    }

    if (visibleIndices.empty())
    {
        ImGui::TextDisabled(scope == EvaluationCaseScope::AllScenes ? "No evaluation cases."
                                                                    : "No evaluation cases for this scene.");
    }
    else if (app.m_selectedEvaluationStateIndex >= 0)
    {
        const size_t selectedIndex = static_cast<size_t>(app.m_selectedEvaluationStateIndex);
        RtPbrSurvey::EvaluationState& state = states[selectedIndex];
        ImGui::SeparatorText("Selected Case");
        ImGui::SetNextItemWidth(280.0f);
        ImGui::InputText("Name", &state.name);
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextMultiline("Comment", &state.comment, ImVec2(0.0f, 58.0f));
        ImGui::Text("Scene: %s", state.sceneName.c_str());

        if (scope == EvaluationCaseScope::AllScenes)
        {
            if (ImGui::Button("Open Scene"))
            {
                std::string error;
                app.m_evaluationStatus = app.RestoreEvaluationState(state, &error) ? "Evaluation case restored."
                                                                                   : "Restore failed: " + error;
            }
        }
        else
        {
            if (ImGui::Button("Restore Case"))
            {
                std::string error;
                app.m_evaluationStatus = app.RestoreEvaluationState(state, &error) ? "Evaluation case restored."
                                                                                   : "Restore failed: " + error;
            }
            ImGui::SameLine();
            if (ImGui::Button("Update Case State"))
            {
                const RtPbrSurvey::EvaluationState previousState = state;
                std::string error;
                if (!app.CaptureEvaluationState(state, &error))
                {
                    state = previousState;
                    app.m_evaluationStatus = "Capture failed: " + error;
                }
                else if (!app.m_evaluationStates.Save(&error))
                {
                    state = previousState;
                    app.m_evaluationStatus = "Save failed: " + error;
                }
                else
                {
                    app.m_evaluationStatus = "Evaluation case state updated.";
                }
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Save Changes"))
        {
            std::string error;
            app.m_evaluationStatus =
                app.m_evaluationStates.Save(&error) ? "Evaluation case changes saved." : "Save failed: " + error;
        }
        ImGui::SameLine();
        if (ImGui::Button("Delete"))
        {
            app.m_evaluationDeleteCandidateId = state.id;
            ImGui::OpenPopup("Delete Evaluation Case?");
        }

        if (scope == EvaluationCaseScope::CurrentScene)
        {
            ImGui::SeparatorText("ROI");
            bool roiChanged = ImGui::Checkbox("Enabled##EvaluationRoi", &state.roi.enabled);
            float roiOrigin[] = {state.roi.x, state.roi.y};
            float roiSize[] = {state.roi.width, state.roi.height};
            roiChanged |= ImGui::DragFloat2("Origin", roiOrigin, 0.005f, 0.0f, 1.0f, "%.3f");
            roiChanged |= ImGui::DragFloat2("Size", roiSize, 0.005f, 0.0f, 1.0f, "%.3f");
            if (roiChanged)
            {
                state.roi.x = roiOrigin[0];
                state.roi.y = roiOrigin[1];
                state.roi.width = roiSize[0];
                state.roi.height = roiSize[1];
                state.roi.Sanitize();
                app.m_evaluationRoi = state.roi;
            }
        }

        ImGui::SeparatorText("Test Items");
        if (ImGui::Button("Add Test Item"))
        {
            RtPbrSurvey::EvaluationTestItem item;
            item.id = app.m_evaluationStates.NextTestItemId(state);
            item.prompt = "Check item";
            state.testItems.push_back(std::move(item));
        }

        std::optional<size_t> removeItemIndex;
        for (size_t i = 0; i < state.testItems.size(); ++i)
        {
            RtPbrSurvey::EvaluationTestItem& item = state.testItems[i];
            ImGui::PushID(static_cast<int>(item.id));
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputText("Check", &item.prompt);
            int judgmentKind = static_cast<int>(item.judgmentKind);
            ImGui::SetNextItemWidth(130.0f);
            if (ImGui::Combo("Judgment", &judgmentKind, "Score 1-5\0True / False\0"))
            {
                item.judgmentKind = static_cast<RtPbrSurvey::EvaluationJudgmentKind>(judgmentKind);
            }
            ImGui::SameLine();
            if (item.judgmentKind == RtPbrSurvey::EvaluationJudgmentKind::Score1To5)
            {
                ImGui::SetNextItemWidth(150.0f);
                ImGui::SliderInt("Result", &item.score, 1, 5);
            }
            else
            {
                ImGui::Checkbox("Result", &item.booleanValue);
                ImGui::SameLine();
                ImGui::TextUnformatted(item.booleanValue ? "true" : "false");
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Remove"))
            {
                removeItemIndex = i;
            }
            ImGui::Separator();
            ImGui::PopID();
        }
        if (removeItemIndex.has_value())
        {
            state.testItems.erase(state.testItems.begin() + static_cast<ptrdiff_t>(*removeItemIndex));
        }
    }

    if (ImGui::BeginPopupModal("Delete Evaluation Case?", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        const auto candidate = std::find_if(states.begin(),
                                            states.end(),
                                            [&app](const RtPbrSurvey::EvaluationState& state)
                                            { return state.id == app.m_evaluationDeleteCandidateId; });
        if (candidate == states.end())
        {
            ImGui::CloseCurrentPopup();
        }
        else
        {
            ImGui::TextUnformatted("Delete evaluation case?");
            ImGui::Separator();
            ImGui::Text("Case: %s", candidate->name.c_str());
            ImGui::Text("Scene: %s", candidate->sceneName.c_str());
            ImGui::Spacing();
            if (ImGui::Button("Yes", ImVec2(90.0f, 0.0f)))
            {
                const size_t erasedIndex = static_cast<size_t>(std::distance(states.begin(), candidate));
                RtPbrSurvey::EvaluationState removed = std::move(*candidate);
                states.erase(states.begin() + static_cast<ptrdiff_t>(erasedIndex));
                std::string error;
                if (!app.m_evaluationStates.Save(&error))
                {
                    states.insert(states.begin() + static_cast<ptrdiff_t>(erasedIndex), std::move(removed));
                    app.m_evaluationStatus = "Delete failed: " + error;
                }
                else
                {
                    app.m_selectedEvaluationStateIndex =
                        states.empty() ? -1
                                       : (std::min)(static_cast<int>(erasedIndex), static_cast<int>(states.size()) - 1);
                    app.m_evaluationStatus = "Evaluation case deleted.";
                }
                app.m_evaluationDeleteCandidateId = 0;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("No", ImVec2(90.0f, 0.0f)))
            {
                app.m_evaluationDeleteCandidateId = 0;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SetItemDefaultFocus();
        }
        ImGui::EndPopup();
    }

    if (!app.m_evaluationStatus.empty())
    {
        ImGui::TextWrapped("%s", app.m_evaluationStatus.c_str());
    }
    ImGui::End();
}

} // namespace App
