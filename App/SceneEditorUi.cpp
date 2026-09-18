#include "stdafx.h"

#include "App/SceneEditorUi.h"

#include "App/RtPbrSurveyApp.h"

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
    const std::filesystem::path sceneRoot = std::filesystem::current_path() / "Assets" / "Scenes";
    std::vector<std::filesystem::path> sceneFiles;
    std::error_code error;
    if (!std::filesystem::is_directory(sceneRoot, error))
    {
        return sceneFiles;
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
    std::sort(sceneFiles.begin(), sceneFiles.end());
    return sceneFiles;
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

    if (ImGui::CollapsingHeader("Scene Camera and Environment"))
    {
        bool sceneSettingsCommitted = false;
        float cameraPosition[3] = {document.camera.position.x, document.camera.position.y, document.camera.position.z};
        if (ImGui::InputFloat3("Camera Position", cameraPosition))
        {
            session.BeginEdit();
            document.camera.position = {cameraPosition[0], cameraPosition[1], cameraPosition[2]};
        }
        sceneSettingsCommitted = sceneSettingsCommitted || ImGui::IsItemDeactivatedAfterEdit();
        float cameraTarget[3] = {document.camera.target.x, document.camera.target.y, document.camera.target.z};
        if (ImGui::InputFloat3("Camera Target", cameraTarget))
        {
            session.BeginEdit();
            document.camera.target = {cameraTarget[0], cameraTarget[1], cameraTarget[2]};
        }
        sceneSettingsCommitted = sceneSettingsCommitted || ImGui::IsItemDeactivatedAfterEdit();
        float verticalFovDegrees = document.camera.verticalFovDegrees;
        if (ImGui::SliderFloat("Camera Vertical FOV", &verticalFovDegrees, 10.0f, 120.0f))
        {
            session.BeginEdit();
            document.camera.verticalFovDegrees = verticalFovDegrees;
        }
        sceneSettingsCommitted = sceneSettingsCommitted || ImGui::IsItemDeactivatedAfterEdit();

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
    ImGui::End();
}

} // namespace App
