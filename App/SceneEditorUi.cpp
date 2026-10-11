#include "stdafx.h"

#include "App/SceneEditorUi.h"

#include "App/RtPbrSurveyApp.h"
#include "Ui/DirectLightUi.h"
#include "Scene/CameraView.h"
#include "Scene/CameraProjection.h"
#include "Scene/SceneGraph.h"
#include "third_party/ImGuizmo/ImGuizmo.h"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <functional>
#include <algorithm>
#include <cfloat>
#include <filesystem>
#include <vector>

namespace
{
std::vector<std::filesystem::path> FindSceneEditorSceneFiles()
{
    const std::filesystem::path assetsRoot = std::filesystem::current_path() / "Assets";
    const std::filesystem::path sceneRoots[] = {
        assetsRoot / "Scenes", assetsRoot / "Scene" / "PathTracingValidation"};
    std::vector<std::filesystem::path> sceneFiles;
    for (const std::filesystem::path& sceneRoot : sceneRoots)
    {
        std::error_code error;
        if (!std::filesystem::is_directory(sceneRoot, error))
        {
            continue;
        }
        for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(sceneRoot, error))
        {
            if (error)
            {
                break;
            }
            if (!entry.is_directory(error))
            {
                continue;
            }
            const std::filesystem::path scenePath = entry.path() / "scene.json";
            if (std::filesystem::is_regular_file(scenePath, error))
            {
                sceneFiles.push_back(scenePath);
            }
        }
    }
    std::sort(sceneFiles.begin(), sceneFiles.end());
    return sceneFiles;
}
bool SceneActionButton(const char* label, bool needsAction)
{
    if (needsAction)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.75f, 0.2f, 1.0f));
    }
    const bool clicked = ImGui::Button(label);
    if (needsAction)
    {
        ImGui::PopStyleColor();
    }
    return clicked;
}
} // namespace

namespace App
{

void DrawSceneEditorStartUi(RtPbrSurveyApp& app)
{
    static std::vector<std::filesystem::path> sceneFiles = FindSceneEditorSceneFiles();
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
    if (ImGui::Button("Refresh Scene List"))
    {
        sceneFiles = FindSceneEditorSceneFiles();
    }
    if (ImGui::BeginListBox("Saved Scenes", ImVec2(-FLT_MIN, 80.0f)))
    {
        for (const std::filesystem::path& scenePath : sceneFiles)
        {
            const std::string sceneName = scenePath.parent_path().filename().generic_string();
            const bool selected = app.m_sceneEditorLoadPath == scenePath.generic_string();
            if (ImGui::Selectable(sceneName.c_str(), selected))
            {
                app.m_sceneEditorLoadPath = scenePath.generic_string();
            }
        }
        ImGui::EndListBox();
    }
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
    std::string windowTitle = "Scene Editor";
    if (!app.m_sceneEditorDocumentPath.empty())
    {
        const std::u8string sceneFolder = std::filesystem::path(app.m_sceneEditorDocumentPath).parent_path().filename().u8string();
        windowTitle += " - ";
        windowTitle.append(sceneFolder.begin(), sceneFolder.end());
    }
    else if (app.m_sceneEditorSession.has_value())
    {
        windowTitle += " - " + app.m_sceneEditorSession->Document().name;
    }
    windowTitle += "###Scene Editor";
    ImGui::SetNextWindowSize(ImVec2(960.0f, 560.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin(windowTitle.c_str());
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
    std::string description = document.description;
    if (ImGui::InputTextMultiline("Description", &description, ImVec2(-FLT_MIN, ImGui::GetTextLineHeight() * 4.0f)))
    {
        session.BeginEdit();
        document.description = std::move(description);
    }
    if (ImGui::IsItemDeactivatedAfterEdit())
    {
        session.CommitEdit();
    }
    ImGui::Text("Nodes: %zu   Assets: %zu   Materials: %zu",
                document.nodes.size(), document.assets.size(), document.materials.size());
    ImGui::SameLine();
    ImGui::Text("Selected: %zu", session.SelectedNodeIds().size());
    ImGui::Text("Document: %s", app.m_sceneEditorDocumentPath.empty() ? "Unsaved" : app.m_sceneEditorDocumentPath.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled(session.IsModified() ? "Modified" : "Saved");
    if (ImGui::CollapsingHeader("SubWindow"))
    {
        ImGui::Checkbox("Transform Window", &app.m_sceneEditorShowTransformWindow);
    }
    ImGui::Separator();

    ImGui::InputText("Save Path", &app.m_sceneEditorSavePath);
    if (SceneActionButton("Save", session.IsModified()))
    {
        std::string error;
        if (!app.SaveSceneEditorDocument(false, &error))
        {
            app.m_sceneEditorStatus = "Save failed: " + error;
        }
    }
    ImGui::SameLine();
    if (SceneActionButton("Save As", session.IsModified()))
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

    if (ImGui::CollapsingHeader("Capture Session"))
    {
        app.DrawCaptureSessionUi();
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

    // Document edits are refused before the Document changes, so a refused operation cannot leave
    // the Document and the preview diverging.
    std::string editReason;
    const bool editAllowed = app.CanEditSceneEditorDocument(editReason);
    ImGui::BeginDisabled(!editAllowed);

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
    if (ImGui::Button("Copy Selected"))
    {
        std::string error;
        if (session.CopySelectedSubtree(&error))
        {
            app.m_sceneEditorStatus = "Copied selected subtree.";
        }
        else
        {
            app.m_sceneEditorStatus = "Copy failed: " + error;
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!session.CanPasteSubtree());
    if (ImGui::Button("Paste"))
    {
        std::string error;
        if (session.PasteSubtree(&error))
        {
            rebuildPreview();
        }
        else
        {
            app.m_sceneEditorStatus = "Paste failed: " + error;
        }
    }
    ImGui::EndDisabled();
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
    static std::string gltfAssetPath = "Assets/Models/DamagedHelmet/glTF/DamagedHelmet.gltf";
    ImGui::InputText("glTF Asset Path", &gltfAssetPath);
    ImGui::SameLine();
    if (ImGui::Button("Add glTF"))
    {
        std::string error;
        if (app.AddSceneEditorGltfNode(gltfAssetPath, &error))
        {
            app.m_sceneEditorStatus = "Added glTF: " + gltfAssetPath;
        }
        else
        {
            app.m_sceneEditorStatus = "Add glTF failed: " + error;
        }
    }
    if (ImGui::Button("Remove Unused Resources"))
    {
        size_t removedAssets = 0;
        size_t removedMaterials = 0;
        if (session.RemoveUnusedResources(&removedAssets, &removedMaterials))
        {
            app.m_sceneEditorStatus = "Removed unused resources: " + std::to_string(removedAssets) +
                                      " assets, " + std::to_string(removedMaterials) + " materials.";
        }
        else
        {
            app.m_sceneEditorStatus = "No unused resources to remove.";
        }
    }

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
    ImGui::EndDisabled();
    if (!editAllowed)
    {
        ImGui::TextWrapped("%s", editReason.c_str());
    }
    if (ImGui::Button("Back to TopMenu"))
    {
        app.RequestReturnToTopMenu();
        if (app.NeedsSceneEditorDecision() && !app.IsCaptureWorkPending())
        {
            ImGui::OpenPopup("Unsaved Scene Changes");
        }
        else
        {
            ImGui::End();
            return;
        }
    }

    if (app.NeedsSceneEditorDecision() && !app.IsCaptureWorkPending())
    {
        ImGui::OpenPopup("Unsaved Scene Changes");
    }
    if (ImGui::BeginPopupModal("Unsaved Scene Changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextUnformatted("The Scene Document has unsaved changes.");
        ImGui::TextUnformatted("Save before continuing?");
        ImGui::TextWrapped("Pending operation: %s.", app.GetPendingHostActionName());
        if (app.IsCaptureWorkPending())
        {
            ImGui::TextWrapped("Capture output is still being saved. The pending operation runs after capture completes.");
        }
        if (ImGui::Button("Save and Continue"))
        {
            app.ResolveSceneEditorPendingAction(true, false);
            if (!app.NeedsSceneEditorDecision())
            {
                ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
                ImGui::End();
                return;
            }
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
        if (app.m_sceneEditorStatus.rfind("Save failed: ", 0) == 0)
        {
            ImGui::TextWrapped("%s", app.m_sceneEditorStatus.c_str());
        }
        ImGui::EndPopup();
    }

    if (!editAllowed)
    {
        ImGui::End();
        return;
    }

    ImGui::Separator();
    ImGui::Columns(3, "SceneEditorColumns", true);

    ImGui::TextUnformatted("Hierarchy");
    ImGui::Separator();
    std::string draggedNodeId;
    std::optional<std::string> droppedParentId;
    bool hasDroppedParent = false;
    std::function<void(const std::optional<std::string>&)> drawNodes;
    drawNodes = [&](const std::optional<std::string>& parentId)
    {
        for (const RtPbrSurvey::SceneNode& node : document.nodes)
        {
            if (node.parentId != parentId)
            {
                continue;
            }
            const bool selected = session.IsNodeSelected(node.id);
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
                if (ImGui::GetIO().KeyCtrl)
                {
                    session.ToggleNodeSelection(node.id);
                }
                else
                {
                    session.SelectNode(node.id);
                }
                app.UpdateSceneEditorSelectionOverlay();
            }
            if (ImGui::BeginDragDropSource())
            {
                ImGui::SetDragDropPayload("SceneEditorNode", node.id.c_str(), node.id.size() + 1);
                ImGui::Text("Move %s", node.name.c_str());
                ImGui::EndDragDropSource();
            }
            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SceneEditorNode"))
                {
                    if (payload->DataSize > 1)
                    {
                        const char* sourceId = static_cast<const char*>(payload->Data);
                        draggedNodeId.assign(sourceId, static_cast<size_t>(payload->DataSize - 1));
                        droppedParentId = node.id;
                        hasDroppedParent = true;
                    }
                }
                ImGui::EndDragDropTarget();
            }
            if (opened && hasChildren)
            {
                drawNodes(node.id);
                ImGui::TreePop();
            }
        }
    };
    drawNodes(std::nullopt);
    ImGui::TextDisabled("Drop here to move a node to Root.");
    if (ImGui::BeginDragDropTarget())
    {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SceneEditorNode"))
        {
            if (payload->DataSize > 1)
            {
                const char* sourceId = static_cast<const char*>(payload->Data);
                draggedNodeId.assign(sourceId, static_cast<size_t>(payload->DataSize - 1));
                droppedParentId.reset();
                hasDroppedParent = true;
            }
        }
        ImGui::EndDragDropTarget();
    }
    if (hasDroppedParent && !draggedNodeId.empty())
    {
        session.SelectNode(draggedNodeId);
        std::string error;
        if (!session.ReparentSelectedNodePreservingWorld(droppedParentId, &error))
        {
            app.m_sceneEditorStatus = "Hierarchy move failed: " + error;
        }
        else
        {
            rebuildPreview();
        }
    }

    ImGui::NextColumn();
    ImGui::TextUnformatted("3D Preview");
    ImGui::Separator();
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
    ImGui::TextWrapped("%s", reinterpret_cast<const char*>(u8"\u9078\u629e\u3057\u305f\u8981\u7d20\u306b\u306f\u30013D\u30d3\u30e5\u30fc\u306b\u8d64\u30fb\u7dd1\u30fb\u9752\u306e\u5ea7\u6a19\u8ef8\u304c\u8868\u793a\u3055\u308c\u307e\u3059\u3002"));
    ImGui::PopStyleColor();
    ImGui::TextDisabled("Ctrl+Click the renderer to select a scene node.");
    ImGui::TextDisabled("Transform controls apply to the primary selection.");

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
        ImGui::Text("Type: %s", selectedNode->type == RtPbrSurvey::SceneNodeType::Empty ? "Empty Node" :
                                       selectedNode->type == RtPbrSurvey::SceneNodeType::Gltf ? "glTF" : "Primitive");
        static std::string editedNodeId;
        static std::string editedNodeName;
        if (editedNodeId != selectedNode->id)
        {
            editedNodeId = selectedNode->id;
            editedNodeName = selectedNode->name;
        }
        ImGui::InputText("Node Name", &editedNodeName);
        ImGui::SameLine();
        if (ImGui::Button("Apply Node Name"))
        {
            std::string error;
            if (session.RenameSelectedNode(editedNodeName, &error))
            {
                app.m_sceneEditorStatus = "Node renamed.";
            }
            else if (!error.empty())
            {
                app.m_sceneEditorStatus = "Rename failed: " + error;
                editedNodeName = selectedNode->name;
            }
        }
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
                static std::string editedMaterialId;
                static std::string editedMaterialName;
                if (editedMaterialId != editableMaterial->id)
                {
                    editedMaterialId = editableMaterial->id;
                    editedMaterialName = editableMaterial->name;
                }
                ImGui::InputText("Material Name", &editedMaterialName);
                ImGui::SameLine();
                if (ImGui::Button("Apply Material Name"))
                {
                    std::string error;
                    if (session.RenameMaterial(editableMaterial->id, editedMaterialName, &error))
                    {
                        app.m_sceneEditorStatus = "Material renamed.";
                    }
                    else if (!error.empty())
                    {
                        app.m_sceneEditorStatus = "Rename failed: " + error;
                        editedMaterialName = editableMaterial->name;
                    }
                }
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
            if (ImGui::Button("Duplicate Material and Assign"))
            {
                std::string materialId;
                std::string error;
                if (session.DuplicateMaterialForSelectedPrimitive(&materialId, &error))
                {
                    app.m_sceneEditorStatus = "Duplicated and assigned material: " + materialId;
                    rebuildPreview();
                }
                else
                {
                    app.m_sceneEditorStatus = "Material duplication failed: " + error;
                }
            }

            bool primitiveCommitted = false;
            auto editPrimitiveFloat = [&session, &primitiveCommitted](const char* label, float& value, float minimum)
            {
                float editedValue = value;
                if (ImGui::DragFloat(label, &editedValue, 0.01f, minimum, 1000.0f))
                {
                    session.BeginEdit();
                    value = editedValue;
                }
                primitiveCommitted = primitiveCommitted || ImGui::IsItemDeactivatedAfterEdit();
            };
            auto editPrimitiveCount = [&session, &primitiveCommitted](const char* label, uint32_t& value, int minimum)
            {
                int editedValue = static_cast<int>(value);
                if (ImGui::DragInt(label, &editedValue, 1.0f, minimum, 256))
                {
                    session.BeginEdit();
                    value = static_cast<uint32_t>(std::clamp(editedValue, minimum, 256));
                }
                primitiveCommitted = primitiveCommitted || ImGui::IsItemDeactivatedAfterEdit();
            };

            ImGui::SeparatorText("Primitive Shape");
            RtPbrSurvey::ScenePrimitive& primitive = selectedNode->primitive;
            switch (primitive.kind)
            {
            case RtPbrSurvey::ScenePrimitiveKind::Cube:
                editPrimitiveFloat("Size", primitive.size, 0.001f);
                break;
            case RtPbrSurvey::ScenePrimitiveKind::Sphere:
                editPrimitiveFloat("Radius", primitive.radius, 0.001f);
                editPrimitiveCount("Stacks", primitive.stacks, 2);
                editPrimitiveCount("Slices", primitive.slices, 3);
                break;
            case RtPbrSurvey::ScenePrimitiveKind::Plane:
                editPrimitiveFloat("Width", primitive.width, 0.001f);
                editPrimitiveFloat("Depth", primitive.depth, 0.001f);
                break;
            case RtPbrSurvey::ScenePrimitiveKind::Cylinder:
                editPrimitiveFloat("Radius", primitive.radius, 0.001f);
                editPrimitiveFloat("Height", primitive.height, 0.001f);
                editPrimitiveCount("Radial Segments", primitive.radialSegments, 3);
                break;
            }
            if (primitiveCommitted)
            {
                session.CommitEdit();
                rebuildPreview();
            }
        }
        else if (selectedNode->type == RtPbrSurvey::SceneNodeType::Gltf)
        {
            const RtPbrSurvey::SceneAsset* asset = nullptr;
            for (const RtPbrSurvey::SceneAsset& candidate : document.assets)
            {
                if (candidate.id == selectedNode->assetId)
                {
                    asset = &candidate;
                    break;
                }
            }
            ImGui::Text("Asset ID: %s", selectedNode->assetId.c_str());
            if (asset != nullptr)
            {
                ImGui::TextWrapped("Asset Path: %s", asset->path.c_str());
            }
            else
            {
                ImGui::TextDisabled("Referenced glTF asset is missing from this document.");
            }
        }
    }
    ImGui::Columns(1);

    if (ImGui::CollapsingHeader("Scene Camera and Environment"))
    {
        ImGui::TextWrapped("%s", reinterpret_cast<const char*>(u8"\u80cc\u666f\u306e3D\u30d3\u30e5\u30fc\u306b\u306f\u7de8\u96c6\u4e2d\u306e\u30b7\u30fc\u30f3\u304c\u8868\u793a\u3055\u308c\u307e\u3059\u3002\u30de\u30a6\u30b9\u3068\u30ad\u30fc\u30dc\u30fc\u30c9\u3067\u30ab\u30e1\u30e9\u3092\u64cd\u4f5c\u3067\u304d\u307e\u3059\u3002"));
        ImGui::Separator();
        Engine::CameraState storedCamera;
        storedCamera.pos = {document.camera.position.x, document.camera.position.y, document.camera.position.z};
        storedCamera.gazePoint = {document.camera.target.x, document.camera.target.y, document.camera.target.z};
        storedCamera.up = {document.camera.up.x, document.camera.up.y, document.camera.up.z};
        storedCamera.fov = document.camera.verticalFovDegrees;
        storedCamera.projection = document.camera.projection == RtPbrSurvey::SceneCameraProjection::Perspective
            ? Engine::CameraProjection::Perspective : Engine::CameraProjection::Orthographic;
        storedCamera.orthographicHeight = document.camera.orthographicHeight;
        storedCamera.nearZ = document.camera.nearZ;
        storedCamera.farZ = document.camera.farZ;
        storedCamera.lensShiftX = document.camera.lensShiftX;
        storedCamera.lensShiftY = document.camera.lensShiftY;
        const DirectX::XMFLOAT3 storedRotation = Engine::GetCameraRotationRadians(storedCamera);
        ImGui::TextUnformatted("Scene Camera");
        ImGui::Text("Position: %.3f, %.3f, %.3f", storedCamera.pos.x, storedCamera.pos.y, storedCamera.pos.z);
        ImGui::Text("Rotation XYZ: %.3f, %.3f, %.3f deg",
                    DirectX::XMConvertToDegrees(storedRotation.x), DirectX::XMConvertToDegrees(storedRotation.y),
                    DirectX::XMConvertToDegrees(storedRotation.z));
        ImGui::Text("FOV: %.3f deg", document.camera.verticalFovDegrees);
        const bool hasCamera = app.m_loadedScene != nullptr && app.m_loadedScene == app.m_sceneEditorPreviewScene.get();
        ImGui::BeginDisabled(!hasCamera);
        if (ImGui::Button("Reset Camera"))
        {
            Engine::CameraState& current = app.m_loadedScene->GetScene().camera;
            current.pos = storedCamera.pos;
            current.gazePoint = storedCamera.gazePoint;
            current.up = storedCamera.up;
            current.rot = storedRotation;
            current.fov = document.camera.verticalFovDegrees;
            current.projection = document.camera.projection == RtPbrSurvey::SceneCameraProjection::Perspective
                ? Engine::CameraProjection::Perspective : Engine::CameraProjection::Orthographic;
            current.orthographicHeight = document.camera.orthographicHeight;
            current.nearZ = document.camera.nearZ;
            current.farZ = document.camera.farZ;
            current.lensShiftX = document.camera.lensShiftX;
            current.lensShiftY = document.camera.lensShiftY;
            app.m_sceneRenderer.SetCamera(current);
        }
        ImGui::SameLine();
        const bool cameraNeedsUpdate = hasCamera &&
            !Engine::CameraViewParametersMatch(app.m_loadedScene->GetScene().camera, storedCamera);
        if (SceneActionButton("Update Scene Camera", cameraNeedsUpdate))
        {
            const Engine::CameraState& current = app.m_loadedScene->GetScene().camera;
            session.BeginEdit();
            document.camera.position = {current.pos.x, current.pos.y, current.pos.z};
            document.camera.target = {current.gazePoint.x, current.gazePoint.y, current.gazePoint.z};
            document.camera.up = {current.up.x, current.up.y, current.up.z};
            document.camera.verticalFovDegrees = current.fov;
            document.camera.projection = current.projection == Engine::CameraProjection::Perspective
                ? RtPbrSurvey::SceneCameraProjection::Perspective : RtPbrSurvey::SceneCameraProjection::Orthographic;
            document.camera.orthographicHeight = current.orthographicHeight;
            document.camera.nearZ = current.nearZ;
            document.camera.farZ = current.farZ;
            document.camera.lensShiftX = current.lensShiftX;
            document.camera.lensShiftY = current.lensShiftY;
            session.CommitEdit();
        }
        if (hasCamera)
        {
            Engine::CameraState& current = app.m_loadedScene->GetScene().camera;
            bool cameraChanged = false;
            float position[3] = {current.pos.x, current.pos.y, current.pos.z};
            if (ImGui::InputFloat3("Camera Position", position))
            {
                if (std::isfinite(position[0]) && std::isfinite(position[1]) && std::isfinite(position[2]))
                {
                    Engine::SetCameraPosition(current, {position[0], position[1], position[2]});
                    cameraChanged = true;
                }
            }
            const DirectX::XMFLOAT3 radians = Engine::GetCameraRotationRadians(current);
            float degrees[3] = {DirectX::XMConvertToDegrees(radians.x), DirectX::XMConvertToDegrees(radians.y),
                                DirectX::XMConvertToDegrees(radians.z)};
            if (ImGui::InputFloat3("Camera Rotation XYZ (deg)", degrees))
            {
                if (std::isfinite(degrees[0]) && std::isfinite(degrees[1]) && std::isfinite(degrees[2]))
                {
                    Engine::SetCameraRotationRadians(current, {DirectX::XMConvertToRadians(degrees[0]),
                        DirectX::XMConvertToRadians(degrees[1]), DirectX::XMConvertToRadians(degrees[2])});
                    cameraChanged = true;
                }
            }
            if (ImGui::SliderFloat("Camera Vertical FOV", &current.fov, 10.0f, 120.0f))
            {
                cameraChanged = true;
            }
            if (cameraChanged)
            {
                app.m_sceneRenderer.SetCamera(current);
            }
        }
        ImGui::EndDisabled();
        bool sceneSettingsCommitted = false;

        ImGui::Separator();
        bool iblEnabled = document.environment.iblEnabled;
        if (ImGui::Checkbox("Environment IBL Enabled", &iblEnabled))
        {
            session.BeginEdit();
            document.environment.iblEnabled = iblEnabled;
            session.CommitEdit();
            rebuildPreview();
        }
        float skyColor[3] = {
            document.environment.skyColor.x,
            document.environment.skyColor.y,
            document.environment.skyColor.z};
        if (ImGui::ColorEdit3("Environment Sky Color", skyColor))
        {
            session.BeginEdit();
            document.environment.skyColor = {skyColor[0], skyColor[1], skyColor[2]};
        }
        sceneSettingsCommitted = sceneSettingsCommitted || ImGui::IsItemDeactivatedAfterEdit();
        float groundColor[3] = {
            document.environment.groundColor.x,
            document.environment.groundColor.y,
            document.environment.groundColor.z};
        if (ImGui::ColorEdit3("Environment Ground Color", groundColor))
        {
            session.BeginEdit();
            document.environment.groundColor = {groundColor[0], groundColor[1], groundColor[2]};
        }
        sceneSettingsCommitted = sceneSettingsCommitted || ImGui::IsItemDeactivatedAfterEdit();
        float lightIntensity = document.environment.lightIntensity;
        if (ImGui::SliderFloat("Environment Light Intensity", &lightIntensity, 0.0f, 20.0f))
        {
            session.BeginEdit();
            document.environment.lightIntensity = lightIntensity;
        }
        sceneSettingsCommitted = sceneSettingsCommitted || ImGui::IsItemDeactivatedAfterEdit();
        float backgroundIntensity = document.environment.backgroundIntensity;
        if (ImGui::SliderFloat("Environment Background Intensity", &backgroundIntensity, 0.0f, 5.0f))
        {
            session.BeginEdit();
            document.environment.backgroundIntensity = backgroundIntensity;
        }
        sceneSettingsCommitted = sceneSettingsCommitted || ImGui::IsItemDeactivatedAfterEdit();
        if (sceneSettingsCommitted)
        {
            session.CommitEdit();
            rebuildPreview();
        }
    }

    if (ImGui::CollapsingHeader("Render Preset"))
    {
        ImGui::Text("Reference: %s", document.renderPresetPath.c_str());
        ImGui::TextDisabled(app.m_sceneEditorPresetDirty ? "Preset: Modified" : "Preset: Saved");
        const bool canUsePresetFile = !app.m_sceneEditorDocumentPath.empty();
        ImGui::BeginDisabled(!canUsePresetFile);
        if (ImGui::Button("Save Preset"))
        {
            std::string error;
            if (app.SaveSceneEditorRenderPreset(&error))
            {
                app.m_sceneEditorStatus = "Render preset saved.";
            }
            else
            {
                app.m_sceneEditorStatus = "Save Preset failed: " + error;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Reload Preset"))
        {
            std::string error;
            if (app.ReloadSceneEditorRenderPreset(&error))
            {
                app.m_sceneEditorStatus = "Render preset reloaded.";
            }
            else
            {
                app.m_sceneEditorStatus = "Reload Preset failed: " + error;
            }
        }
        ImGui::EndDisabled();

        if (ImGui::TreeNode("Lights"))
        {
            RtPbrSurveyEngine::LightingParams lighting = app.m_sceneRenderer.GetLightingParams();
            if (RtPbrSurvey::DrawDirectLightControls(lighting))
            {
                app.m_sceneRenderer.SetLightingParams(lighting);
                app.m_sceneEditorPresetDirty = true;
            }
            ImGui::TextWrapped("Lights are stored in the render preset. Use Save Preset above to persist changes.");
            ImGui::TreePop();
        }

        const RtPbrSurveyEngine::RenderingPath renderingPath = app.m_sceneRenderer.GetRenderingPath();
        if (ImGui::RadioButton("Forward", renderingPath == RtPbrSurveyEngine::RenderingPath::Forward))
        {
            app.m_sceneRenderer.SetRenderingPath(RtPbrSurveyEngine::RenderingPath::Forward);
            app.m_renderingPath = RtPbrSurveyEngine::RenderingPath::Forward;
            app.m_sceneEditorPresetDirty = true;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Deferred", renderingPath == RtPbrSurveyEngine::RenderingPath::Deferred))
        {
            app.m_sceneRenderer.SetRenderingPath(RtPbrSurveyEngine::RenderingPath::Deferred);
            app.m_renderingPath = RtPbrSurveyEngine::RenderingPath::Deferred;
            app.m_sceneEditorPresetDirty = true;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Path Tracing", renderingPath == RtPbrSurveyEngine::RenderingPath::PathTracing))
        {
            app.m_sceneRenderer.SetRenderingPath(RtPbrSurveyEngine::RenderingPath::PathTracing);
            app.m_renderingPath = RtPbrSurveyEngine::RenderingPath::PathTracing;
            app.m_sceneEditorPresetDirty = true;
        }
        if (app.m_sceneRenderer.GetRenderingPath() == RtPbrSurveyEngine::RenderingPath::PathTracing)
        {
            RtPbrSurveyEngine::PathTracingSettings settings = app.m_sceneRenderer.GetPathTracingSettings();
            bool changed = ImGui::Checkbox("Accumulate", &settings.accumulate);
            int maxBounces = static_cast<int>(settings.maxBounces);
            if (ImGui::SliderInt("Max Bounces", &maxBounces, 1, 16))
            {
                settings.maxBounces = static_cast<UINT>(maxBounces);
                changed = true;
            }
            int samplesPerFrame = static_cast<int>(settings.samplesPerFrame);
            if (ImGui::SliderInt("Samples / Frame", &samplesPerFrame, 1, 16))
            {
                settings.samplesPerFrame = static_cast<UINT>(samplesPerFrame);
                changed = true;
            }
            changed |= ImGui::Checkbox("Direct Lighting", &settings.directLightingEnabled);
            changed |= ImGui::Checkbox("Environment", &settings.environmentEnabled);
            changed |= ImGui::Checkbox("Emissive", &settings.emissiveEnabled);
            int emissiveSamplingMode = static_cast<int>(settings.emissiveSamplingMode);
            if (ImGui::Combo("Emissive Sampling", &emissiveSamplingMode, "BSDF-only\0NEE-only\0MIS (BSDF + NEE)\0"))
            {
                settings.emissiveSamplingMode = static_cast<UINT>(emissiveSamplingMode);
                changed = true;
            }
            int output = static_cast<int>(settings.debugOutput);
            if (ImGui::Combo("Path Tracing Output", &output, "Albedo + Emissive\0World Normal\0Emissive\0Radiance\0"))
            {
                settings.debugOutput = static_cast<RtPbrSurveyEngine::PathTracingDebugOutput>(output);
                changed = true;
            }
            if (changed)
            {
                app.m_sceneRenderer.SetPathTracingSettings(settings);
                app.m_sceneEditorPresetDirty = true;
            }
            RtPbrSurveyEngine::ShadowSettings shadow = app.m_sceneRenderer.GetShadowSettings();
            if (ImGui::Checkbox("Shadows", &shadow.enabled))
            {
                app.m_sceneRenderer.SetShadowSettings(shadow);
                app.m_sceneEditorPresetDirty = true;
            }
            if (ImGui::Button("Reset Accumulation"))
            {
                app.m_sceneRenderer.ResetPathTracingAccumulation();
            }
        }
        if (!canUsePresetFile)
        {
            ImGui::TextDisabled("Save the Scene Document before saving or reloading its preset.");
        }
    }

    if (!app.m_sceneEditorStatus.empty())
    {
        ImGui::Separator();
        ImGui::TextWrapped("%s", app.m_sceneEditorStatus.c_str());
    }

    if (ImGui::CollapsingHeader("glTF Assets"))
    {
        if (ImGui::Button("Reload All glTF Assets"))
        {
            if (rebuildPreview())
            {
                app.m_sceneEditorStatus = "Reloaded glTF assets from the current Scene Document.";
            }
        }
        if (document.assets.empty())
        {
            ImGui::TextDisabled("No glTF assets are registered in this Scene Document.");
        }
        for (const RtPbrSurvey::SceneAsset& asset : document.assets)
        {
            const size_t useCount = static_cast<size_t>(std::count_if(document.nodes.begin(), document.nodes.end(), [&asset](const RtPbrSurvey::SceneNode& node)
            {
                return node.type == RtPbrSurvey::SceneNodeType::Gltf && node.assetId == asset.id;
            }));
            ImGui::Separator();
            ImGui::Text("%s  (%zu node%s)", asset.id.c_str(), useCount, useCount == 1 ? "" : "s");
            ImGui::TextWrapped("%s", asset.path.c_str());
        }
    }
    ImGui::End();
}

void DrawSceneEditorTransformUi(RtPbrSurveyApp& app)
{
    const bool wasUsing = app.m_sceneEditorGizmoUsing;
    app.m_sceneEditorGizmoUsing = false;
    app.m_sceneEditorGizmoCapturesMouse = false;
    if (!app.m_sceneEditorSession.has_value())
    {
        return;
    }
    SceneEditorSession& session = *app.m_sceneEditorSession;
    RtPbrSurvey::SceneNode* node = session.SelectedNode();
    std::string editReason;
    const bool editAllowed = app.CanEditSceneEditorDocument(editReason);
    if (node == nullptr || !editAllowed || app.m_loadedScene == nullptr)
    {
        if (wasUsing || app.m_sceneEditorTransformEditPending)
        {
            session.CommitEdit();
            app.m_sceneEditorTransformEditPending = false;
        }
        return;
    }

    const auto rebuildPreview = [&app]()
    {
        std::string error;
        if (!app.RebuildSceneEditorPreview(&error))
        {
            app.m_sceneEditorStatus = "Transform preview failed: " + error;
        }
    };
    const auto localMatrix = [&node]()
    {
        const RtPbrSurvey::SceneTransform& transform = node->transform;
        return DirectX::XMMatrixScaling(transform.scale.x, transform.scale.y, transform.scale.z) *
            DirectX::XMMatrixRotationQuaternion(DirectX::XMQuaternionNormalize(DirectX::XMVectorSet(
                transform.rotation.x, transform.rotation.y, transform.rotation.z, transform.rotation.w))) *
            DirectX::XMMatrixTranslation(transform.translation.x, transform.translation.y, transform.translation.z);
    };

    const bool windowOpen = app.m_sceneEditorShowTransformWindow;
    if (windowOpen)
    {
        ImGui::SetNextWindowSize(ImVec2(430.0f, 290.0f), ImGuiCond_FirstUseEver);
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + (std::max)(0.0f, viewport->WorkSize.x - 450.0f),
                                     viewport->WorkPos.y + 20.0f), ImGuiCond_FirstUseEver);
    }
    const char* nodeType = "Unknown";
    switch (node->type)
    {
    case RtPbrSurvey::SceneNodeType::Gltf:
        nodeType = "Mesh (glTF)";
        break;
    case RtPbrSurvey::SceneNodeType::Primitive:
        nodeType = "Mesh (Primitive)";
        break;
    case RtPbrSurvey::SceneNodeType::Empty:
        nodeType = "Empty Node";
        break;
    }
    const std::string transformTitle = "Transform - " + node->name + " [" + nodeType + "]###Transform";
    const bool visible = windowOpen && ImGui::Begin(transformTitle.c_str(), &app.m_sceneEditorShowTransformWindow);
    bool changed = false;
    bool committed = false;
    if (visible)
    {
        ImGui::Text("%s (Local)", node->name.c_str());
        ImGui::Text("Type: %s", nodeType);
        ImGui::PushID(node->id.c_str());
        ImGui::Checkbox("Transform Gizumo", &app.m_sceneEditorTransformGizmoEnabled);
        ImGui::BeginDisabled(wasUsing);
        ImGui::RadioButton("Move", &app.m_sceneEditorTransformTool, 0);
        ImGui::SameLine();
        ImGui::RadioButton("Rotate", &app.m_sceneEditorTransformTool, 1);
        ImGui::SameLine();
        ImGui::RadioButton("Scale", &app.m_sceneEditorTransformTool, 2);
        if (app.m_sceneEditorTransformTool == 0)
        {
            ImGui::DragFloat("Move Step", &app.m_sceneEditorTranslationStep, 0.01f, 0.01f, 100.0f, "%.2f");
        }
        else if (app.m_sceneEditorTransformTool == 1)
        {
            ImGui::DragFloat("Rotate Step", &app.m_sceneEditorRotationStepDegrees, 1.0f, 1.0f, 180.0f, "%.0f deg");
        }
        else
        {
            ImGui::DragFloat("Scale Step", &app.m_sceneEditorScaleStep, 0.01f, 0.01f, 100.0f, "%.2f");
        }
        const auto applyTransformAxis = [&app, &session, &rebuildPreview](int axis, float direction)
        {
            RtPbrSurvey::SceneNode* node = session.SelectedNode();
            if (node == nullptr)
            {
                return;
            }
            session.CommitEdit();
            app.m_sceneEditorTransformEditPending = false;
            session.BeginEdit();
            const auto selectAxisComponent = [axis](RtPbrSurvey::SceneFloat3& value) -> float&
            {
                if (axis == 0)
                {
                    return value.x;
                }
                if (axis == 1)
                {
                    return value.y;
                }
                return value.z;
            };
            if (app.m_sceneEditorTransformTool == 0)
            {
                selectAxisComponent(node->transform.translation) += direction * app.m_sceneEditorTranslationStep;
            }
            else if (app.m_sceneEditorTransformTool == 1)
            {
                const DirectX::XMVECTOR currentRotation = DirectX::XMVectorSet(node->transform.rotation.x,
                                                                                 node->transform.rotation.y,
                                                                                 node->transform.rotation.z,
                                                                                 node->transform.rotation.w);
                const DirectX::XMVECTOR rotationAxis = axis == 0 ? DirectX::XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f) :
                                                       axis == 1 ? DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f) :
                                                                   DirectX::XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);
                const DirectX::XMVECTOR deltaRotation = DirectX::XMQuaternionRotationAxis(
                    rotationAxis, DirectX::XMConvertToRadians(direction * app.m_sceneEditorRotationStepDegrees));
                DirectX::XMFLOAT4 rotation = {};
                DirectX::XMStoreFloat4(&rotation, DirectX::XMQuaternionNormalize(
                                                    DirectX::XMQuaternionMultiply(currentRotation, deltaRotation)));
                node->transform.rotation = {rotation.x, rotation.y, rotation.z, rotation.w};
            }
            else
            {
                float& scale = selectAxisComponent(node->transform.scale);
                scale = (std::max)(0.01f, scale + direction * app.m_sceneEditorScaleStep);
            }
            session.CommitEdit();
            rebuildPreview();
        };
        ImGui::BeginDisabled(session.SelectedNode() == nullptr);
        if (ImGui::Button("X-"))
        {
            applyTransformAxis(0, -1.0f);
        }
        ImGui::SameLine();
        if (ImGui::Button("X+"))
        {
            applyTransformAxis(0, 1.0f);
        }
        ImGui::SameLine();
        if (ImGui::Button("Y-"))
        {
            applyTransformAxis(1, -1.0f);
        }
        ImGui::SameLine();
        if (ImGui::Button("Y+"))
        {
            applyTransformAxis(1, 1.0f);
        }
        ImGui::SameLine();
        if (ImGui::Button("Z-"))
        {
            applyTransformAxis(2, -1.0f);
        }
        ImGui::SameLine();
        if (ImGui::Button("Z+"))
        {
            applyTransformAxis(2, 1.0f);
        }
        ImGui::EndDisabled();
        ImGui::Separator();
        float position[3] = {node->transform.translation.x, node->transform.translation.y, node->transform.translation.z};
        if (ImGui::InputFloat3("Position", position) &&
            std::isfinite(position[0]) && std::isfinite(position[1]) && std::isfinite(position[2]))
        {
            session.BeginEdit();
            node->transform.translation = {position[0], position[1], position[2]};
            changed = true;
        }
        committed |= ImGui::IsItemDeactivatedAfterEdit();
        DirectX::XMFLOAT4X4 local;
        DirectX::XMStoreFloat4x4(&local, localMatrix());
        float translation[3], rotation[3], scale[3];
        ImGuizmo::DecomposeMatrixToComponents(&local._11, translation, rotation, scale);
        if (ImGui::InputFloat3("Rotation XYZ", rotation, "%.3f deg") &&
            std::isfinite(rotation[0]) && std::isfinite(rotation[1]) && std::isfinite(rotation[2]))
        {
            ImGuizmo::RecomposeMatrixFromComponents(translation, rotation, scale, &local._11);
            DirectX::XMVECTOR s, q, t;
            if (DirectX::XMMatrixDecompose(&s, &q, &t, DirectX::XMLoadFloat4x4(&local)))
            {
                DirectX::XMFLOAT4 quaternion;
                DirectX::XMStoreFloat4(&quaternion, DirectX::XMQuaternionNormalize(q));
                session.BeginEdit();
                node->transform.rotation = {quaternion.x, quaternion.y, quaternion.z, quaternion.w};
                changed = true;
            }
        }
        committed |= ImGui::IsItemDeactivatedAfterEdit();
        if (ImGui::InputFloat3("Scale", scale) &&
            std::isfinite(scale[0]) && std::isfinite(scale[1]) && std::isfinite(scale[2]) &&
            scale[0] > 0.0f && scale[1] > 0.0f && scale[2] > 0.0f)
        {
            session.BeginEdit();
            node->transform.scale = {scale[0], scale[1], scale[2]};
            changed = true;
        }
        committed |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::EndDisabled();
        ImGui::PopID();
    }
    if (windowOpen)
    {
        ImGui::End();
    }
    app.m_sceneEditorTransformEditPending |= changed;
    if (committed || (!visible && !wasUsing && app.m_sceneEditorTransformEditPending))
    {
        session.CommitEdit();
        app.m_sceneEditorTransformEditPending = false;
    }
    if (changed)
    {
        session.MarkModified();
        rebuildPreview();
    }

    if (!app.m_sceneEditorTransformGizmoEnabled)
    {
        ImGuizmo::BeginFrame();
        ImGuizmo::PushID(node->id.c_str());
        ImGuizmo::Enable(false);
        ImGuizmo::PopID();
        if (wasUsing)
        {
            session.CommitEdit();
        }
        return;
    }

    RtPbrSurvey::SceneGraphEvaluation graph;
    if (!RtPbrSurvey::EvaluateSceneGraph(session.Document(), graph, nullptr))
    {
        return;
    }
    const DirectX::XMFLOAT4X4* nodeWorld = graph.FindWorld(node->id, session.Document());
    if (nodeWorld == nullptr)
    {
        return;
    }
    DirectX::XMMATRIX parentWorld = DirectX::XMMatrixIdentity();
    if (node->parentId.has_value())
    {
        const DirectX::XMFLOAT4X4* parent = graph.FindWorld(*node->parentId, session.Document());
        if (parent == nullptr)
        {
            return;
        }
        parentWorld = DirectX::XMLoadFloat4x4(parent);
    }
    DirectX::XMFLOAT4X4 world = *nodeWorld;
    const Engine::CameraState& camera = app.m_loadedScene->GetScene().camera;
    const ImVec2 displaySize = ImGui::GetIO().DisplaySize;
    DirectX::XMFLOAT4X4 view, projection;
    DirectX::XMStoreFloat4x4(&view, Engine::CreateCameraViewMatrix(camera));
    DirectX::XMStoreFloat4x4(&projection, Engine::CreateCameraProjectionMatrix(
        camera, displaySize.x / (std::max)(displaySize.y, 1.0f)));
    ImGuizmo::BeginFrame();
    ImGuizmo::SetDrawlist(ImGui::GetBackgroundDrawList());
    ImGuizmo::SetRect(0.0f, 0.0f, displaySize.x, displaySize.y);
    ImGuizmo::SetOrthographic(camera.projection == Engine::CameraProjection::Orthographic);
    ImGuizmo::PushID(node->id.c_str());
    ImGuizmo::Enable(!ImGui::IsAnyItemActive() || wasUsing);
    const ImGuizmo::OPERATION operation = app.m_sceneEditorTransformTool == 0 ? ImGuizmo::TRANSLATE :
        app.m_sceneEditorTransformTool == 1 ? ImGuizmo::ROTATE : ImGuizmo::SCALE;
    const bool manipulated = ImGuizmo::Manipulate(&view._11, &projection._11, operation, ImGuizmo::LOCAL, &world._11);
    app.m_sceneEditorGizmoUsing = ImGuizmo::IsUsing();
    app.m_sceneEditorGizmoCapturesMouse = ImGuizmo::IsOver() || app.m_sceneEditorGizmoUsing;
    if (manipulated)
    {
        const DirectX::XMMATRIX local = DirectX::XMLoadFloat4x4(&world) * DirectX::XMMatrixInverse(nullptr, parentWorld);
        DirectX::XMVECTOR s, q, t;
        if (DirectX::XMMatrixDecompose(&s, &q, &t, local))
        {
            DirectX::XMFLOAT3 scale, position;
            DirectX::XMFLOAT4 rotation;
            DirectX::XMStoreFloat3(&scale, s);
            DirectX::XMStoreFloat3(&position, t);
            DirectX::XMStoreFloat4(&rotation, DirectX::XMQuaternionNormalize(q));
            const DirectX::XMMATRIX reconstructed = DirectX::XMMatrixScalingFromVector(s) *
                DirectX::XMMatrixRotationQuaternion(q) * DirectX::XMMatrixTranslationFromVector(t);
            DirectX::XMFLOAT4X4 original, result;
            DirectX::XMStoreFloat4x4(&original, local);
            DirectX::XMStoreFloat4x4(&result, reconstructed);
            bool representable = scale.x > 0.0f && scale.y > 0.0f && scale.z > 0.0f;
            for (size_t index = 0; index < 16; ++index)
            {
                const float value = (&original._11)[index];
                representable &= std::isfinite(value) && std::isfinite((&result._11)[index]) &&
                    std::abs(value - (&result._11)[index]) <= 0.0001f * (std::max)(1.0f, std::abs(value));
            }
            if (representable)
            {
                session.BeginEdit();
                node->transform.translation = {position.x, position.y, position.z};
                node->transform.rotation = {rotation.x, rotation.y, rotation.z, rotation.w};
                node->transform.scale = {scale.x, scale.y, scale.z};
                session.MarkModified();
                rebuildPreview();
            }
            else
            {
                app.m_sceneEditorStatus = "Gizmo transform cannot be represented as local Position / Rotation / Scale.";
            }
        }
        else
        {
            app.m_sceneEditorStatus = "Gizmo transform cannot be decomposed into local Position / Rotation / Scale.";
        }
    }
    ImGuizmo::PopID();
    if (wasUsing && !app.m_sceneEditorGizmoUsing)
    {
        session.CommitEdit();
    }
}

} // namespace App
