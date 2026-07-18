#include "stdafx.h"
#include "SceneSelectUi.h"
#include "RtPbrSurveyApp.h"

#include <cstring>

#include <imgui.h>

namespace App
{

void DrawSceneSelectUi(RtPbrSurveyApp& app)
{
    ImGui::SetNextWindowSize(ImVec2(360, 360), ImGuiCond_FirstUseEver);
    ImGui::Begin("Scene Select");

    const int sceneCount = static_cast<int>(app.m_sampleScenes.size());
    const int benchmarkStart = app.m_gltfViewerCount;
    const int demoSceneStart = app.m_gltfViewerCount + app.m_gltfSceneCount;

    ImGui::Text("glTF Viewer");
    ImGui::Separator();
    ImGui::PushID("gltf-viewer");
    const bool viewerSelectionActive = app.m_selectedSceneIndex >= 0 && app.m_selectedSceneIndex < app.m_gltfViewerCount;
    const char* viewerPreview =
        viewerSelectionActive ? app.m_sampleScenes[static_cast<size_t>(app.m_selectedSceneIndex)]->Name() : "Select viewer asset";
    if (ImGui::BeginCombo("Asset", viewerPreview))
    {
        for (int i = 0; i < app.m_gltfViewerCount; i++)
        {
            const bool available = app.m_sampleScenes[static_cast<size_t>(i)]->Available();
            const bool selected = app.m_selectedSceneIndex == i;
            ImGui::BeginDisabled(!available);
            if (ImGui::Selectable(app.m_sampleScenes[static_cast<size_t>(i)]->Name(), selected))
            {
                app.m_selectedSceneIndex = i;
            }
            ImGui::EndDisabled();
            if (selected)
            {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    ImGui::BeginDisabled(!viewerSelectionActive);
    if (ImGui::Button("Load Scene"))
    {
        app.OpenSelectedScene();
    }
    ImGui::EndDisabled();
    ImGui::PopID();

    ImGui::Dummy(ImVec2(0.0f, 4.0f));
    ImGui::Text("glTF Grid Benchmark");
    ImGui::Separator();
    ImGui::PushID("gltf-grid-benchmark");
    const bool benchmarkSelectionActive =
        app.m_selectedSceneIndex >= benchmarkStart && app.m_selectedSceneIndex < benchmarkStart + app.m_gltfSceneCount;
    const char* benchmarkPreview = benchmarkSelectionActive
        ? app.m_sampleScenes[static_cast<size_t>(app.m_selectedSceneIndex)]->Name()
        : "Select benchmark asset";
    if (ImGui::BeginCombo("Asset", benchmarkPreview))
    {
        for (int i = benchmarkStart; i < benchmarkStart + app.m_gltfSceneCount; i++)
        {
            const bool available = app.m_sampleScenes[static_cast<size_t>(i)]->Available();
            const bool selected = app.m_selectedSceneIndex == i;
            ImGui::BeginDisabled(!available);
            if (ImGui::Selectable(app.m_sampleScenes[static_cast<size_t>(i)]->Name(), selected))
            {
                app.m_selectedSceneIndex = i;
            }
            ImGui::EndDisabled();
            if (selected)
            {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    ImGui::BeginDisabled(!benchmarkSelectionActive);
    if (ImGui::Button("Load Scene"))
    {
        app.OpenSelectedScene();
    }
    ImGui::EndDisabled();
    ImGui::PopID();

    ImGui::Dummy(ImVec2(0.0f, 4.0f));
    ImGui::Text("Demo Scenes");
    ImGui::Separator();
    const bool demoSelectionActive = app.m_selectedSceneIndex >= demoSceneStart && app.m_selectedSceneIndex < sceneCount;
    bool shadowTestStarted = false;
    bool shadowTestOpen = false;
    static constexpr const char* kShadowTestPrefix = "Shadow Test: ";
    static constexpr size_t kShadowTestPrefixLength = 13;
    for (int i = demoSceneStart; i < sceneCount; i++)
    {
        const char* sceneName = app.m_sampleScenes[static_cast<size_t>(i)]->Name();
        if (strncmp(sceneName, kShadowTestPrefix, kShadowTestPrefixLength) == 0)
        {
            if (!shadowTestStarted)
            {
                ImGui::Dummy(ImVec2(0.0f, 2.0f));
                shadowTestOpen = ImGui::TreeNodeEx("Shadow Test", ImGuiTreeNodeFlags_DefaultOpen);
                shadowTestStarted = true;
            }
            if (shadowTestOpen)
            {
                ImGui::RadioButton(sceneName + kShadowTestPrefixLength, &app.m_selectedSceneIndex, i);
            }
        }
        else
        {
            if (shadowTestOpen)
            {
                ImGui::TreePop();
                shadowTestOpen = false;
            }
            ImGui::RadioButton(sceneName, &app.m_selectedSceneIndex, i);
        }
    }
    if (shadowTestOpen)
    {
        ImGui::TreePop();
    }
    ImGui::BeginDisabled(!demoSelectionActive);
    if (ImGui::Button("Load Scene"))
    {
        app.OpenSelectedScene();
    }
    ImGui::EndDisabled();

    const bool hasLoadedScene = app.m_loadedSceneIndex >= 0;
    const bool selectedSceneIsLoaded = hasLoadedScene && app.m_selectedSceneIndex == app.m_loadedSceneIndex;
    if (!hasLoadedScene)
    {
        ImGui::Text("Scene CPU data not loaded");
    }
    else if (!selectedSceneIsLoaded)
    {
        ImGui::Text("Scene CPU data will reload");
    }
    else if (!app.m_sceneResourcesLoaded)
    {
        ImGui::Text("Scene GPU resources unloaded");
    }

    ImGui::End();
}

} // namespace App
