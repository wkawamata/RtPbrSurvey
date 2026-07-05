#include "stdafx.h"
#include "SceneSelectUi.h"
#include "HelloTextureApp.h"

#include <imgui.h>

namespace App
{

void DrawSceneSelectUi(HelloTextureApp& app)
{
    ImGui::SetNextWindowSize(ImVec2(360, 360), ImGuiCond_FirstUseEver);
    ImGui::Begin("Scene Select");

    const int sceneCount = static_cast<int>(app.m_sampleScenes.size());
    const int benchmarkStart = app.m_gltfViewerCount;
    const int demoSceneStart = app.m_gltfViewerCount + app.m_gltfSceneCount;

    ImGui::Text("glTF Viewer");
    ImGui::Separator();
    ImGui::PushID("gltf-viewer");
    for (int i = 0; i < app.m_gltfViewerCount; i++)
    {
        const bool available = app.m_sampleScenes[static_cast<size_t>(i)]->Available();
        ImGui::BeginDisabled(!available);
        ImGui::RadioButton(app.m_sampleScenes[static_cast<size_t>(i)]->Name(), &app.m_selectedSceneIndex, i);
        ImGui::EndDisabled();
    }
    ImGui::PopID();

    ImGui::Dummy(ImVec2(0.0f, 4.0f));
    ImGui::Text("glTF Grid Benchmark");
    ImGui::Separator();
    ImGui::PushID("gltf-grid-benchmark");
    for (int i = benchmarkStart; i < benchmarkStart + app.m_gltfSceneCount; i++)
    {
        const bool available = app.m_sampleScenes[static_cast<size_t>(i)]->Available();
        ImGui::BeginDisabled(!available);
        ImGui::RadioButton(app.m_sampleScenes[static_cast<size_t>(i)]->Name(), &app.m_selectedSceneIndex, i);
        ImGui::EndDisabled();
    }
    ImGui::PopID();

    ImGui::Dummy(ImVec2(0.0f, 4.0f));
    ImGui::Text("Demo Scenes");
    ImGui::Separator();
    for (int i = demoSceneStart; i < sceneCount; i++)
    {
        ImGui::RadioButton(app.m_sampleScenes[static_cast<size_t>(i)]->Name(), &app.m_selectedSceneIndex, i);
    }

    ImGui::Dummy(ImVec2(0.0f, 8.0f));
    if (ImGui::Button("Load Scene"))
    {
        app.OpenSelectedScene();
    }

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
