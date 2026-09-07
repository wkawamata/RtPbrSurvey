#pragma once

#include "imgui.h"
#include <algorithm>

namespace ImGuiWidgets
{

static constexpr float kMinControlLabelWidth = 180.0f;

inline float CalcButtonWidth(const char* text)
{
    return ImGui::CalcTextSize(text).x + ImGui::GetStyle().FramePadding.x * 2.0f;
}

inline bool SimpleDetailMode(const char* id, bool* detailed)
{
    const char* labels[] = {"Simple", "Detail"};
    const float width = (std::max)(80.0f, (std::max)(CalcButtonWidth(labels[0]), CalcButtonWidth(labels[1])));
    bool changed = false;

    ImGui::PushID(id);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, ImGui::GetStyle().ItemSpacing.y));
    for (int index = 0; index < 2; ++index)
    {
        const bool selected = *detailed == (index == 1);
        if (selected)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        }
        if (ImGui::Button(labels[index], ImVec2(width, 0.0f)) && !selected)
        {
            *detailed = index == 1;
            changed = true;
        }
        if (selected)
        {
            ImGui::PopStyleColor(2);
        }
        if (index == 0)
        {
            ImGui::SameLine();
        }
    }
    ImGui::PopStyleVar();
    ImGui::PopID();
    return changed;
}

inline float BeginSliderControl(const char* label, float totalButtonsWidth)
{
    const ImGuiStyle& s = ImGui::GetStyle();
    const float labelWidth = (std::max)(kMinControlLabelWidth, ImGui::CalcTextSize(label).x) + s.ItemSpacing.x;

    ImGui::PushID(label);

    return (std::max)(1.0f, ImGui::GetContentRegionAvail().x - totalButtonsWidth - labelWidth);
}

inline void DrawSliderControlLabel(const char* label)
{
    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemSpacing.x);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
}

inline void EndSliderControl()
{
    ImGui::PopID();
}

inline bool SliderFloatWithControls(const char* label,
                                    float* value,
                                    float min,
                                    float max,
                                    float delta,
                                    float defaultValue,
                                    const char* format = "%.3f",
                                    ImGuiSliderFlags flags = 0)
{
    const float btnW = CalcButtonWidth(">");
    const float totalButtons = btnW * 3.0f;
    const float sliderWidth = BeginSliderControl(label, totalButtons);

    ImGui::PushItemWidth(sliderWidth);
    bool changed = ImGui::SliderFloat("##slider", value, min, max, format, flags);
    ImGui::PopItemWidth();

    ImGui::SameLine(0.0f, 0.0f);
    if (ImGui::Button("<", ImVec2(btnW, 0.0f)))
    {
        *value = std::clamp(*value - delta, min, max);
        changed = true;
    }

    ImGui::SameLine(0.0f, 0.0f);
    if (ImGui::Button(">", ImVec2(btnW, 0.0f)))
    {
        *value = std::clamp(*value + delta, min, max);
        changed = true;
    }

    ImGui::SameLine(0.0f, 0.0f);
    if (ImGui::Button("|", ImVec2(btnW, 0.0f)))
    {
        *value = defaultValue;
        changed = true;
    }

    DrawSliderControlLabel(label);
    EndSliderControl();
    return changed;
}

inline bool SliderIntWithControls(
    const char* label, int* value, int min, int max, int delta, int defaultValue, const char* format = "%d")
{
    const float btnW = CalcButtonWidth(">");
    const float totalButtons = btnW * 3.0f;
    const float sliderWidth = BeginSliderControl(label, totalButtons);

    ImGui::PushItemWidth(sliderWidth);
    bool changed = ImGui::SliderInt("##slider", value, min, max, format);
    ImGui::PopItemWidth();

    ImGui::SameLine(0.0f, 0.0f);
    if (ImGui::Button("<", ImVec2(btnW, 0.0f)))
    {
        *value = std::clamp(*value - delta, min, max);
        changed = true;
    }

    ImGui::SameLine(0.0f, 0.0f);
    if (ImGui::Button(">", ImVec2(btnW, 0.0f)))
    {
        *value = std::clamp(*value + delta, min, max);
        changed = true;
    }

    ImGui::SameLine(0.0f, 0.0f);
    if (ImGui::Button("|", ImVec2(btnW, 0.0f)))
    {
        *value = defaultValue;
        changed = true;
    }

    DrawSliderControlLabel(label);
    EndSliderControl();
    return changed;
}

inline bool IntStepperWithControls(const char* label, int* value, int min, int max, int delta, int defaultValue)
{
    const float btnW = CalcButtonWidth(">");

    ImGui::PushID(label);

    bool changed = false;
    if (ImGui::Button("<", ImVec2(btnW, 0.0f)))
    {
        *value = std::clamp(*value - delta, min, max);
        changed = true;
    }

    ImGui::SameLine(0.0f, 0.0f);
    if (ImGui::Button(">", ImVec2(btnW, 0.0f)))
    {
        *value = std::clamp(*value + delta, min, max);
        changed = true;
    }

    ImGui::SameLine(0.0f, 0.0f);
    if (ImGui::Button("|", ImVec2(btnW, 0.0f)))
    {
        *value = std::clamp(defaultValue, min, max);
        changed = true;
    }

    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemSpacing.x);
    *value = std::clamp(*value, min, max);
    ImGui::AlignTextToFramePadding();
    ImGui::Text("%d / %d", *value, max);

    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemSpacing.x);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    EndSliderControl();
    return changed;
}

inline bool SliderFloat3WithControls(const char* label,
                                     float* v,
                                     float v_min,
                                     float v_max,
                                     float delta,
                                     const float* defaultValue,
                                     const char* format = "%.3f",
                                     ImGuiSliderFlags flags = 0)
{
    const float btnW = CalcButtonWidth(">");
    const float totalButtons = btnW * 3.0f;
    const float sliderWidth = BeginSliderControl(label, totalButtons);

    ImGui::PushItemWidth(sliderWidth);
    bool changed = ImGui::SliderFloat3("##slider", v, v_min, v_max, format, flags);
    ImGui::PopItemWidth();

    ImGui::SameLine(0.0f, 0.0f);
    if (ImGui::Button("<", ImVec2(btnW, 0.0f)))
    {
        v[0] = std::clamp(v[0] - delta, v_min, v_max);
        v[1] = std::clamp(v[1] - delta, v_min, v_max);
        v[2] = std::clamp(v[2] - delta, v_min, v_max);
        changed = true;
    }

    ImGui::SameLine(0.0f, 0.0f);
    if (ImGui::Button(">", ImVec2(btnW, 0.0f)))
    {
        v[0] = std::clamp(v[0] + delta, v_min, v_max);
        v[1] = std::clamp(v[1] + delta, v_min, v_max);
        v[2] = std::clamp(v[2] + delta, v_min, v_max);
        changed = true;
    }

    ImGui::SameLine(0.0f, 0.0f);
    if (ImGui::Button("|", ImVec2(btnW, 0.0f)))
    {
        v[0] = defaultValue[0];
        v[1] = defaultValue[1];
        v[2] = defaultValue[2];
        changed = true;
    }

    DrawSliderControlLabel(label);
    EndSliderControl();
    return changed;
}

} // namespace ImGuiWidgets
