#include "stdafx.h"

#include "App/SceneEditorUi.h"

#include "App/RtPbrSurveyApp.h"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <functional>

namespace App
{

void DrawSceneEditorStartUi(RtPbrSurveyApp& app)
{
    ImGui::SetNextWindowSize(ImVec2(480.0f, 250.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("Scene Editor");
    ImGui::TextUnformatted("Create or load a test scene document.");
    ImGui::Separator();

    ImGui::InputText("Scene Name", &app.m_sceneEditorNewName);
    if (ImGui::Button("New Creation"))
    {
        app.CreateNewSceneEditorDocument();
    }

    ImGui::Dummy(ImVec2(0.0f, 8.0f));
    ImGui::InputText("Scene File", &app.m_sceneEditorLoadPath);
    if (ImGui::Button("Load"))
    {
        std::string error;
        if (!app.LoadSceneEditorDocument(app.m_sceneEditorLoadPath, &error))
        {
            app.m_sceneEditorStatus = "Load failed: " + error;
        }
    }

    if (!app.m_sceneEditorStatus.empty())
    {
        ImGui::TextWrapped("%s", app.m_sceneEditorStatus.c_str());
    }
    ImGui::Dummy(ImVec2(0.0f, 8.0f));
    if (ImGui::Button("Back to TopMenu"))
    {
        app.ReturnToTopMenu();
    }
    ImGui::End();
}

void DrawSceneEditorEditUi(RtPbrSurveyApp& app)
{
    ImGui::SetNextWindowSize(ImVec2(960.0f, 560.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("Scene Editor");
    if (!app.m_sceneEditorSession.has_value())
    {
        ImGui::TextUnformatted("No Scene Document is selected.");
        ImGui::BeginDisabled();
        ImGui::Button("Save");
        ImGui::EndDisabled();
        ImGui::End();
        return;
    }

    App::SceneEditorSession& session = *app.m_sceneEditorSession;
    RtPbrSurvey::SceneDocument& document = session.Document();
    ImGui::Text("Scene: %s", document.name.c_str());
    ImGui::Text("Scene ID: %s", document.sceneId.c_str());
    ImGui::Text("Nodes: %zu   Assets: %zu   Materials: %zu",
                document.nodes.size(), document.assets.size(), document.materials.size());
    ImGui::Text("Document: %s", app.m_sceneEditorDocumentPath.empty() ? "Unsaved" : app.m_sceneEditorDocumentPath.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled(session.IsModified() ? "Modified" : "Saved");
    ImGui::Separator();

    ImGui::InputText("Save Path", &app.m_sceneEditorSavePath);
    if (ImGui::Button("Save"))
    {
        std::string error;
        if (!app.SaveSceneEditorDocument(false, &error))
        {
            app.m_sceneEditorStatus = "Save failed: " + error;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Save As"))
    {
        std::string error;
        if (!app.SaveSceneEditorDocument(true, &error))
        {
            app.m_sceneEditorStatus = "Save As failed: " + error;
        }
    }
    ImGui::InputText("Load Scene File", &app.m_sceneEditorLoadPath);
    ImGui::SameLine();
    if (ImGui::Button("Load"))
    {
        app.RequestLoadSceneEditorDocument(app.m_sceneEditorLoadPath);
        ImGui::End();
        return;
    }
    ImGui::SameLine();
    if (ImGui::Button("New Creation"))
    {
        app.RequestNewSceneEditorDocument();
        ImGui::End();
        return;
    }

    auto rebuildPreview = [&app]()
    {
        std::string error;
        if (!app.RebuildSceneEditorPreview(&error))
        {
            app.m_sceneEditorStatus = "Preview update failed: " + error;
            return false;
        }
        app.m_sceneEditorStatus = "Preview updated.";
        return true;
    };

    ImGui::BeginDisabled(!session.CanUndo());
    if (ImGui::Button("Undo"))
    {
        session.Undo();
        rebuildPreview();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!session.CanRedo());
    if (ImGui::Button("Redo"))
    {
        session.Redo();
        rebuildPreview();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();

    if (ImGui::Button("Add Empty"))
    {
        session.AddEmpty();
        rebuildPreview();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(session.SelectedNode() == nullptr);
    if (ImGui::Button("Duplicate Selected"))
    {
        session.DuplicateSelectedSubtree();
        rebuildPreview();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Add Material"))
    {
        std::string materialId;
        session.AddMaterial(&materialId);
        app.m_sceneEditorStatus = "Added material: " + materialId;
    }
    ImGui::SameLine();

    if (ImGui::Button("Add Cube"))
    {
        session.AddPrimitive(RtPbrSurvey::ScenePrimitiveKind::Cube);
        rebuildPreview();
    }
    ImGui::SameLine();
    if (ImGui::Button("Add Sphere"))
    {
        session.AddPrimitive(RtPbrSurvey::ScenePrimitiveKind::Sphere);
        rebuildPreview();
    }
    ImGui::SameLine();
    if (ImGui::Button("Add Plane"))
    {
        session.AddPrimitive(RtPbrSurvey::ScenePrimitiveKind::Plane);
        rebuildPreview();
    }
    ImGui::SameLine();
    if (ImGui::Button("Add Cylinder"))
    {
        session.AddPrimitive(RtPbrSurvey::ScenePrimitiveKind::Cylinder);
        rebuildPreview();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(session.SelectedNode() == nullptr);
    if (ImGui::Button("Delete Selected"))
    {
        session.DeleteSelectedNode();
        rebuildPreview();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Back to TopMenu"))
    {
        app.RequestReturnToTopMenu();
        if (app.m_sceneEditorPendingAction != RtPbrSurveyApp::SceneEditorPendingAction::None)
        {
            ImGui::OpenPopup("Unsaved Scene Changes");
        }
        else
        {
            ImGui::End();
            return;
        }
    }

    if (app.m_sceneEditorPendingAction != RtPbrSurveyApp::SceneEditorPendingAction::None)
    {
        ImGui::OpenPopup("Unsaved Scene Changes");
    }
    if (ImGui::BeginPopupModal("Unsaved Scene Changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextUnformatted("The Scene Document has unsaved changes.");
        ImGui::TextUnformatted("Save before continuing?");
        if (ImGui::Button("Save and Continue"))
        {
            app.ResolveSceneEditorPendingAction(true, false);
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
            ImGui::End();
            return;
        }
        ImGui::SameLine();
        if (ImGui::Button("Discard"))
        {
            app.ResolveSceneEditorPendingAction(false, true);
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
            ImGui::End();
            return;
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
        {
            app.ResolveSceneEditorPendingAction(false, false);
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::Separator();
    ImGui::Columns(3, "SceneEditorColumns", true);

    ImGui::TextUnformatted("Hierarchy");
    ImGui::Separator();
    std::function<void(const std::optional<std::string>&)> drawNodes;
    drawNodes = [&](const std::optional<std::string>& parentId)
    {
        for (const RtPbrSurvey::SceneNode& node : document.nodes)
        {
            if (node.parentId != parentId)
            {
                continue;
            }
            const bool selected = session.SelectedNodeId().has_value() && *session.SelectedNodeId() == node.id;
            const bool hasChildren = std::any_of(document.nodes.begin(), document.nodes.end(), [&node](const RtPbrSurvey::SceneNode& candidate)
            {
                return candidate.parentId.has_value() && *candidate.parentId == node.id;
            });
            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
            if (selected)
            {
                flags |= ImGuiTreeNodeFlags_Selected;
            }
            if (!hasChildren)
            {
                flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
            }
            const bool opened = ImGui::TreeNodeEx(node.id.c_str(), flags, "%s", node.name.c_str());
            if (ImGui::IsItemClicked())
            {
                session.SelectNode(node.id);
            }
            if (opened && hasChildren)
            {
                drawNodes(node.id);
                ImGui::TreePop();
            }
        }
    };
    drawNodes(std::nullopt);

    ImGui::NextColumn();
    ImGui::TextUnformatted("3D Preview");
    ImGui::Separator();
    ImGui::TextWrapped("The renderer behind this editor displays the current document. Mouse and keyboard camera controls remain available.");
    ImGui::TextDisabled("Rebuilds occur after a transform field is committed.");

    ImGui::NextColumn();
    ImGui::TextUnformatted("Inspector");
    ImGui::Separator();
    RtPbrSurvey::SceneNode* selectedNode = session.SelectedNode();
    if (selectedNode == nullptr)
    {
        ImGui::TextDisabled("Select a hierarchy node.");
    }
    else
    {
        ImGui::Text("Name: %s", selectedNode->name.c_str());
        ImGui::Text("ID: %s", selectedNode->id.c_str());
        ImGui::Text("Type: %s", selectedNode->type == RtPbrSurvey::SceneNodeType::Empty ? "Empty" :
                                       selectedNode->type == RtPbrSurvey::SceneNodeType::Gltf ? "glTF" : "Primitive");
        bool visible = selectedNode->visible;
        if (ImGui::Checkbox("Visible", &visible))
        {
            session.BeginEdit();
            selectedNode->visible = visible;
            session.CommitEdit();
            rebuildPreview();
        }
        const char* parentPreview = selectedNode->parentId.has_value() ? selectedNode->parentId->c_str() : "<Root>";
        bool parentChanged = false;
        if (ImGui::BeginCombo("Parent (World Preserved)", parentPreview))
        {
            if (ImGui::Selectable("<Root>", !selectedNode->parentId.has_value()))
            {
                std::string error;
                if (!session.ReparentSelectedNodePreservingWorld(std::nullopt, &error))
                {
                    app.m_sceneEditorStatus = "Parent change failed: " + error;
                }
                else
                {
                    parentChanged = true;
                }
            }
            for (size_t nodeIndex = 0; nodeIndex < document.nodes.size(); ++nodeIndex)
            {
                const RtPbrSurvey::SceneNode& parentCandidate = document.nodes[nodeIndex];
                if (parentCandidate.id == selectedNode->id)
                {
                    continue;
                }
                const bool currentParent = selectedNode->parentId == parentCandidate.id;
                if (ImGui::Selectable(parentCandidate.name.c_str(), currentParent))
                {
                    std::string error;
                    if (!session.ReparentSelectedNodePreservingWorld(parentCandidate.id, &error))
                    {
                        app.m_sceneEditorStatus = "Parent change failed: " + error;
                    }
                    else
                    {
                        parentChanged = true;
                    }
                    break;
                }
            }
            ImGui::EndCombo();
        }
        if (parentChanged)
        {
            rebuildPreview();
            ImGui::Columns(1);
            ImGui::End();
            return;
        }
        if (selectedNode->type == RtPbrSurvey::SceneNodeType::Primitive)
        {
            const RtPbrSurvey::SceneMaterial* currentMaterial = nullptr;
            for (const RtPbrSurvey::SceneMaterial& material : document.materials)
            {
                if (material.id == selectedNode->materialId)
                {
                    currentMaterial = &material;
                    break;
                }
            }
            const char* materialPreview = currentMaterial != nullptr ? currentMaterial->name.c_str() : "<Missing Material>";
            if (ImGui::BeginCombo("Material", materialPreview))
            {
                for (const RtPbrSurvey::SceneMaterial& material : document.materials)
                {
                    const bool selected = selectedNode->materialId == material.id;
                    if (ImGui::Selectable(material.name.c_str(), selected) && !selected)
                    {
                        session.BeginEdit();
                        selectedNode->materialId = material.id;
                        session.CommitEdit();
                        rebuildPreview();
                    }
                }
                ImGui::EndCombo();
            }

            RtPbrSurvey::SceneMaterial* editableMaterial = nullptr;
            for (RtPbrSurvey::SceneMaterial& material : document.materials)
            {
                if (material.id == selectedNode->materialId)
                {
                    editableMaterial = &material;
                    break;
                }
            }
            if (editableMaterial != nullptr)
            {
                float baseColor[3] = {
                    editableMaterial->baseColor.x,
                    editableMaterial->baseColor.y,
                    editableMaterial->baseColor.z};
                bool materialCommitted = false;
                if (ImGui::ColorEdit3("Base Color", baseColor))
                {
                    session.BeginEdit();
                    editableMaterial->baseColor = {baseColor[0], baseColor[1], baseColor[2], 1.0f};
                }
                materialCommitted = materialCommitted || ImGui::IsItemDeactivatedAfterEdit();
                float metallic = editableMaterial->metallic;
                if (ImGui::SliderFloat("Metallic", &metallic, 0.0f, 1.0f))
                {
                    session.BeginEdit();
                    editableMaterial->metallic = metallic;
                }
                materialCommitted = materialCommitted || ImGui::IsItemDeactivatedAfterEdit();
                float roughness = editableMaterial->roughness;
                if (ImGui::SliderFloat("Roughness", &roughness, 0.0f, 1.0f))
                {
                    session.BeginEdit();
                    editableMaterial->roughness = roughness;
                }
                materialCommitted = materialCommitted || ImGui::IsItemDeactivatedAfterEdit();
                if (materialCommitted)
                {
                    session.CommitEdit();
                    rebuildPreview();
                }
            }
        }
        ImGui::Separator();
        bool transformCommitted = false;
        float translation[3] = {
            selectedNode->transform.translation.x,
            selectedNode->transform.translation.y,
            selectedNode->transform.translation.z};
        if (ImGui::InputFloat3("Translation", translation))
        {
            session.BeginEdit();
            selectedNode->transform.translation = {translation[0], translation[1], translation[2]};
        }
        transformCommitted = transformCommitted || ImGui::IsItemDeactivatedAfterEdit();
        float rotation[4] = {
            selectedNode->transform.rotation.x,
            selectedNode->transform.rotation.y,
            selectedNode->transform.rotation.z,
            selectedNode->transform.rotation.w};
        if (ImGui::InputFloat4("Rotation", rotation))
        {
            session.BeginEdit();
            selectedNode->transform.rotation = {rotation[0], rotation[1], rotation[2], rotation[3]};
        }
        transformCommitted = transformCommitted || ImGui::IsItemDeactivatedAfterEdit();
        float scale[3] = {
            selectedNode->transform.scale.x,
            selectedNode->transform.scale.y,
            selectedNode->transform.scale.z};
        if (ImGui::InputFloat3("Scale", scale))
        {
            session.BeginEdit();
            selectedNode->transform.scale = {scale[0], scale[1], scale[2]};
        }
        transformCommitted = transformCommitted || ImGui::IsItemDeactivatedAfterEdit();
        if (transformCommitted)
        {
            session.CommitEdit();
            rebuildPreview();
        }
    }
    ImGui::Columns(1);

    if (!app.m_sceneEditorStatus.empty())
    {
        ImGui::Separator();
        ImGui::TextWrapped("%s", app.m_sceneEditorStatus.c_str());
    }
    ImGui::End();
}

} // namespace App
