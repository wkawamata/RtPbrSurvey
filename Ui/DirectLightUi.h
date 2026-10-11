#pragma once

#include "Shared/DirectLight.h"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <limits>

namespace RtPbrSurvey
{
// Shared by the running-scene controls, renderer tools, and Scene Editor preset panel.
template <typename LightingParams> bool DrawDirectLightControls(LightingParams& params)
{
    bool changed = ImGui::Checkbox("Direct Light", &params.directLightEnabled);
    ImGui::Text("Lights: %zu / %u", params.lights.size(), kMaxDirectLights);
    const ImGuiID selectionKey = ImGui::GetID("SelectedDirectLight");
    ImGuiStorage* storage = ImGui::GetStateStorage();
    uint32_t selectedId = static_cast<uint32_t>(storage->GetInt(selectionKey, 0));
    for (const DirectLight& light : params.lights)
    {
        ImGui::PushID(static_cast<int>(light.id));
        const std::string label = light.name + (light.enabled ? "" : " (disabled)");
        if (ImGui::Selectable(label.c_str(), selectedId == light.id))
        {
            selectedId = light.id;
        }
        ImGui::PopID();
    }
    static uint64_t nextId = 2;
    nextId = (std::max)(nextId, static_cast<uint64_t>(params.primaryShadowLightId) + 1);
    for (const DirectLight& light : params.lights)
    {
        nextId = (std::max)(nextId, static_cast<uint64_t>(light.id) + 1);
    }
    const bool canAdd = params.lights.size() < kMaxDirectLights && nextId <= UINT32_MAX;
    ImGui::BeginDisabled(!canAdd);
    const char* names[] = {"Directional", "Point", "Spot"};
    for (int type = 0; type < 3; ++type)
    {
        if (type != 0)
        {
            ImGui::SameLine();
        }
        const std::string label = std::string("Add ") + names[type];
        if (ImGui::Button(label.c_str()))
        {
            DirectLight light;
            light.id = static_cast<uint32_t>(nextId++);
            light.name = std::string(names[type]) + " " + std::to_string(light.id);
            light.type = static_cast<LightType>(type);
            params.lights.push_back(light);
            selectedId = light.id;
            changed = true;
        }
    }
    ImGui::EndDisabled();
    if (!canAdd)
    {
        ImGui::TextDisabled("Light limit reached; remove a light before adding another.");
    }

    const auto selected = std::find_if(params.lights.begin(),
                                       params.lights.end(),
                                       [selectedId](const DirectLight& light) { return light.id == selectedId; });
    if (selected != params.lights.end())
    {
        DirectLight edited = *selected;
        bool editedChanged = ImGui::InputText("Name", &edited.name);
        editedChanged |= ImGui::Checkbox("Enabled", &edited.enabled);
        int type = static_cast<int>(edited.type);
        if (ImGui::Combo("Type", &type, names, 3))
        {
            edited.type = static_cast<LightType>(type);
            editedChanged = true;
        }
        editedChanged |= ImGui::ColorEdit3("Color (linear RGB)", &edited.color.x, ImGuiColorEditFlags_Float);
        editedChanged |= ImGui::DragFloat(
            "Intensity", &edited.intensity, 0.05f, 0.0f, 10000.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
        if (edited.type != LightType::Directional)
        {
            editedChanged |= ImGui::DragFloat3("Position", &edited.position.x, 0.05f);
            editedChanged |= ImGui::DragFloat(
                "Range", &edited.range, 0.05f, 0.001f, 100000.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
        }
        if (edited.type != LightType::Point)
        {
            editedChanged |= ImGui::DragFloat3("Direction (light travel)", &edited.direction.x, 0.01f, -1.0f, 1.0f);
        }
        if (edited.type == LightType::Spot)
        {
            editedChanged |=
                ImGui::SliderFloat("Inner half-angle (degrees)", &edited.innerCone, 0.0f, edited.outerCone - 0.1f);
            editedChanged |=
                ImGui::SliderFloat("Outer half-angle (degrees)", &edited.outerCone, edited.innerCone + 0.1f, 89.9f);
        }
        if (editedChanged)
        {
            try
            {
                ValidateDirectLight(edited);
                *selected = edited;
                if ((!edited.enabled || edited.type != LightType::Directional) &&
                    params.primaryShadowLightId == edited.id)
                {
                    params.primaryShadowLightId = 0;
                }
                changed = true;
            }
            catch (const std::exception& error)
            {
                ImGui::TextWrapped("%s", error.what());
            }
        }
        bool primary = params.primaryShadowLightId == selectedId;
        ImGui::BeginDisabled(!selected->enabled || selected->type != LightType::Directional);
        if (ImGui::Checkbox("Primary directional shadow", &primary))
        {
            params.primaryShadowLightId = primary ? selectedId : 0;
            changed = true;
        }
        ImGui::EndDisabled();
        ImGui::BeginDisabled(!canAdd);
        if (ImGui::Button("Duplicate Light"))
        {
            DirectLight copy = *selected;
            copy.id = static_cast<uint32_t>(nextId++);
            copy.name += " Copy";
            selectedId = copy.id;
            params.lights.push_back(std::move(copy));
            changed = true;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Delete Light"))
        {
            if (params.primaryShadowLightId == selectedId)
            {
                params.primaryShadowLightId = 0;
            }
            params.lights.erase(std::remove_if(params.lights.begin(),
                                               params.lights.end(),
                                               [selectedId](const DirectLight& light)
                                               { return light.id == selectedId; }),
                                params.lights.end());
            selectedId = 0;
            changed = true;
        }
    }
    storage->SetInt(selectionKey, static_cast<int>(selectedId));
    ImGui::TextWrapped("Relative intensity; Point/Spot use inverse-square attenuation and a smooth range cutoff.");
    ImGui::TextWrapped("Deferred shadows: primary Directional only. Additional lights and Forward are unshadowed.");
    ImGui::TextWrapped("PT multi-light adapter pending: currently evaluates only the primary Directional.");
    return changed;
}
} // namespace RtPbrSurvey
