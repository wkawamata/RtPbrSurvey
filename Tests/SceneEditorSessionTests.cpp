#include "App/SceneEditorSession.h"
#include "Scene/SceneGraph.h"

#include <iostream>

namespace
{
bool Check(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "FAILED: " << message << '\n';
    }
    return condition;
}

bool TestPrimitiveAddAndSubtreeDelete()
{
    RtPbrSurvey::SceneDocument document = RtPbrSurvey::CreateEmptySceneDocument("session-test", "Session Test");
    App::SceneEditorSession session(std::move(document), true);
    bool passed = Check(session.AddPrimitive(RtPbrSurvey::ScenePrimitiveKind::Cube), "cube is added");
    const RtPbrSurvey::SceneNode* cube = session.SelectedNode();
    passed &= Check(cube != nullptr && cube->type == RtPbrSurvey::SceneNodeType::Primitive,
                    "added primitive is selected");
    passed &= Check(session.Document().materials.size() == 1 && cube != nullptr &&
                        cube->materialId == session.Document().materials.front().id,
                    "added primitive receives a default material");

    RtPbrSurvey::SceneNode child;
    child.id = "child";
    child.name = "Child";
    child.parentId = cube->id;
    session.Document().nodes.push_back(child);
    passed &= Check(session.DeleteSelectedNode(), "selected subtree is deleted");
    passed &= Check(session.Document().nodes.empty(), "selected node and descendants are removed");
    passed &= Check(!session.SelectedNodeId().has_value(), "selection clears after deletion");
    return passed;
}

bool TestUndoRedoRestoresDocumentAndDirtyState()
{
    RtPbrSurvey::SceneDocument document = RtPbrSurvey::CreateEmptySceneDocument("history-test", "History Test");
    App::SceneEditorSession session(std::move(document));
    bool passed = Check(session.AddPrimitive(RtPbrSurvey::ScenePrimitiveKind::Cube), "first primitive is added");
    session.MarkSaved();
    passed &= Check(session.AddPrimitive(RtPbrSurvey::ScenePrimitiveKind::Sphere), "second primitive is added");
    passed &= Check(session.IsModified() && session.Document().nodes.size() == 2 && session.CanUndo(),
                    "add creates an undoable dirty operation");
    passed &= Check(session.Undo(), "undo succeeds");
    passed &= Check(!session.IsModified() && session.Document().nodes.size() == 1 && session.CanRedo(),
                    "undo restores the saved document state");
    passed &= Check(session.Redo(), "redo succeeds");
    passed &= Check(session.IsModified() && session.Document().nodes.size() == 2,
                    "redo restores the modified document state");
    return passed;
}

bool TestDuplicateAndReparentSelectedSubtree()
{
    RtPbrSurvey::SceneDocument document = RtPbrSurvey::CreateEmptySceneDocument("hierarchy-test", "Hierarchy Test");
    RtPbrSurvey::SceneNode root;
    root.id = "root";
    root.name = "Root";
    root.transform.translation = {2.0f, 0.0f, 0.0f};
    RtPbrSurvey::SceneNode child;
    child.id = "child";
    child.name = "Child";
    child.parentId = root.id;
    child.transform.translation = {1.0f, 0.0f, 0.0f};
    RtPbrSurvey::SceneNode newParent;
    newParent.id = "new-parent";
    newParent.name = "New Parent";
    newParent.transform.translation = {-4.0f, 0.0f, 0.0f};
    document.nodes = {root, child, newParent};

    App::SceneEditorSession session(std::move(document));
    bool passed = Check(session.SelectNode("root"), "root is selected");
    passed &= Check(session.DuplicateSelectedSubtree(), "subtree duplicates");
    passed &= Check(session.Document().nodes.size() == 5, "duplicate adds root and child");
    const RtPbrSurvey::SceneNode* duplicatedRoot = session.SelectedNode();
    passed &= Check(duplicatedRoot != nullptr && duplicatedRoot->id != "root", "duplicated root is selected with a new ID");
    bool duplicateChildHasDuplicateParent = false;
    if (duplicatedRoot != nullptr)
    {
        for (const RtPbrSurvey::SceneNode& node : session.Document().nodes)
        {
            if (node.id != "child" && node.parentId == duplicatedRoot->id)
            {
                duplicateChildHasDuplicateParent = true;
            }
        }
    }
    passed &= Check(duplicateChildHasDuplicateParent, "duplicated child references duplicated root");

    passed &= Check(session.SelectNode("child"), "original child is selected");
    passed &= Check(session.ReparentSelectedNodePreservingWorld(std::string("new-parent")), "reparent succeeds");
    RtPbrSurvey::SceneGraphEvaluation evaluation;
    std::string error;
    passed &= Check(RtPbrSurvey::EvaluateSceneGraph(session.Document(), evaluation, &error), "reparented hierarchy evaluates");
    const DirectX::XMFLOAT4X4* childWorld = evaluation.FindWorld("child", session.Document());
    passed &= Check(childWorld != nullptr && childWorld->_41 == 3.0f, "reparent preserves child world translation");
    return passed;
}

bool TestMaterialCreationIsUndoable()
{
    RtPbrSurvey::SceneDocument document = RtPbrSurvey::CreateEmptySceneDocument("material-test", "Material Test");
    App::SceneEditorSession session(std::move(document));
    std::string materialId;
    bool passed = Check(session.AddMaterial(&materialId), "material is added");
    passed &= Check(materialId == "material-1" && session.Document().materials.size() == 1,
                    "new material has a unique ID");
    passed &= Check(session.Undo() && session.Document().materials.empty(), "material creation is undoable");
    passed &= Check(session.Redo() && session.Document().materials.size() == 1,
                    "material creation is redoable");
    return passed;
}

bool TestGltfAssetIsSharedAcrossNodes()
{
    RtPbrSurvey::SceneDocument document = RtPbrSurvey::CreateEmptySceneDocument("gltf-test", "glTF Test");
    App::SceneEditorSession session(std::move(document));
    bool passed = Check(session.AddGltfNode("Assets/Models/DamagedHelmet/glTF/DamagedHelmet.gltf"),
                        "first glTF node is added");
    passed &= Check(session.AddGltfNode("Assets/Models/DamagedHelmet/glTF/DamagedHelmet.gltf"),
                    "second glTF node is added");
    passed &= Check(session.Document().assets.size() == 1 && session.Document().nodes.size() == 2,
                    "same glTF path uses one asset and two nodes");
    passed &= Check(session.Document().nodes[0].assetId == session.Document().nodes[1].assetId,
                    "glTF nodes reference the shared asset");
    passed &= Check(session.Undo() && session.Document().assets.size() == 1 && session.Document().nodes.size() == 1,
                    "undo preserves the asset used by the remaining node");
    std::string error;
    passed &= Check(!session.AddGltfNode("C:/absolute/model.gltf", &error) && !error.empty(),
                    "absolute glTF asset path is rejected");
    return passed;
}

bool TestUnusedResourcesAreRemovedAndUndoable()
{
    RtPbrSurvey::SceneDocument document = RtPbrSurvey::CreateEmptySceneDocument("cleanup-test", "Cleanup Test");
    document.assets = {{"asset-used", "Assets/Models/used.gltf"}, {"asset-unused", "Assets/Models/unused.gltf"}};
    document.materials = {{"material-used", "Used"}, {"material-unused", "Unused"}};
    RtPbrSurvey::SceneNode gltfNode;
    gltfNode.id = "gltf";
    gltfNode.name = "Gltf";
    gltfNode.type = RtPbrSurvey::SceneNodeType::Gltf;
    gltfNode.assetId = "asset-used";
    RtPbrSurvey::SceneNode primitiveNode;
    primitiveNode.id = "primitive";
    primitiveNode.name = "Primitive";
    primitiveNode.type = RtPbrSurvey::SceneNodeType::Primitive;
    primitiveNode.materialId = "material-used";
    document.nodes = {gltfNode, primitiveNode};

    App::SceneEditorSession session(std::move(document));
    size_t removedAssets = 0;
    size_t removedMaterials = 0;
    bool passed = Check(session.RemoveUnusedResources(&removedAssets, &removedMaterials), "unused resources are removed");
    passed &= Check(removedAssets == 1 && removedMaterials == 1 && session.Document().assets.size() == 1 &&
                        session.Document().materials.size() == 1,
                    "only unused asset and material are removed");
    passed &= Check(session.Undo() && session.Document().assets.size() == 2 && session.Document().materials.size() == 2,
                    "resource cleanup is undoable");
    return passed;
}

bool TestNodeRenameIsUndoableAndRejectsEmptyNames()
{
    RtPbrSurvey::SceneDocument document = RtPbrSurvey::CreateEmptySceneDocument("rename-test", "Rename Test");
    RtPbrSurvey::SceneNode node;
    node.id = "node";
    node.name = "Original";
    document.nodes.push_back(node);
    App::SceneEditorSession session(std::move(document));
    bool passed = Check(session.SelectNode("node"), "node is selected for rename");
    passed &= Check(session.RenameSelectedNode("Renamed"), "node is renamed");
    passed &= Check(session.SelectedNode()->name == "Renamed" && session.IsModified(), "renamed node is dirty");
    passed &= Check(session.Undo() && session.SelectedNode()->name == "Original", "node rename is undoable");
    std::string error;
    passed &= Check(!session.RenameSelectedNode("   ", &error) && !error.empty(), "empty node name is rejected");
    return passed;
}

bool TestCopyPasteSelectedSubtreeIsUndoable()
{
    RtPbrSurvey::SceneDocument document = RtPbrSurvey::CreateEmptySceneDocument("clipboard-test", "Clipboard Test");
    RtPbrSurvey::SceneNode root;
    root.id = "root";
    root.name = "Root";
    RtPbrSurvey::SceneNode child;
    child.id = "child";
    child.name = "Child";
    child.parentId = root.id;
    document.nodes = {root, child};

    App::SceneEditorSession session(std::move(document));
    bool passed = Check(session.SelectNode("root"), "root is selected for copy");
    passed &= Check(session.CopySelectedSubtree() && session.CanPasteSubtree(), "selected subtree is copied");
    passed &= Check(session.PasteSubtree(), "copied subtree is pasted");
    passed &= Check(session.Document().nodes.size() == 4, "paste adds root and child");
    const RtPbrSurvey::SceneNode* pastedRoot = session.SelectedNode();
    bool pastedChildHasPastedParent = false;
    if (pastedRoot != nullptr)
    {
        for (const RtPbrSurvey::SceneNode& node : session.Document().nodes)
        {
            if (node.id != "child" && node.parentId == pastedRoot->id)
            {
                pastedChildHasPastedParent = true;
            }
        }
    }
    passed &= Check(pastedRoot != nullptr && pastedRoot->id != "root" && pastedChildHasPastedParent,
                    "paste creates a distinct hierarchy");
    passed &= Check(session.Undo() && session.Document().nodes.size() == 2, "paste is undoable");
    return passed;
}

bool TestPrimitiveShapeEditIsUndoable()
{
    RtPbrSurvey::SceneDocument document = RtPbrSurvey::CreateEmptySceneDocument("primitive-edit-test", "Primitive Edit Test");
    App::SceneEditorSession session(std::move(document));
    bool passed = Check(session.AddPrimitive(RtPbrSurvey::ScenePrimitiveKind::Sphere), "sphere is added for shape edit");
    session.MarkSaved();
    session.BeginEdit();
    RtPbrSurvey::SceneNode* sphere = session.SelectedNode();
    sphere->primitive.radius = 2.0f;
    sphere->primitive.stacks = 32;
    sphere->primitive.slices = 48;
    session.CommitEdit();
    passed &= Check(session.IsModified() && sphere->primitive.radius == 2.0f && sphere->primitive.stacks == 32 &&
                        sphere->primitive.slices == 48,
                    "primitive shape edit updates values");
    passed &= Check(session.Undo() && session.SelectedNode()->primitive.radius == 0.5f &&
                        session.SelectedNode()->primitive.stacks == 24 && session.SelectedNode()->primitive.slices == 32,
                    "primitive shape edit is undoable");
    return passed;
}

bool TestSceneSettingsEditIsUndoable()
{
    RtPbrSurvey::SceneDocument document = RtPbrSurvey::CreateEmptySceneDocument("settings-test", "Settings Test");
    App::SceneEditorSession session(std::move(document));
    session.MarkSaved();
    session.BeginEdit();
    session.Document().camera.position = {2.0f, 3.0f, -4.0f};
    session.Document().environment.lightIntensity = 8.0f;
    session.CommitEdit();
    bool passed = Check(session.IsModified() && session.Document().camera.position.x == 2.0f &&
                             session.Document().environment.lightIntensity == 8.0f,
                        "scene settings edit is recorded");
    passed &= Check(session.Undo() && !session.IsModified() && session.Document().camera.position.x == 0.0f &&
                        session.Document().environment.lightIntensity == 6.0f,
                    "undo restores saved camera and environment settings");
    return passed;
}
} // namespace

int main()
{
    return TestPrimitiveAddAndSubtreeDelete() && TestUndoRedoRestoresDocumentAndDirtyState() &&
                   TestDuplicateAndReparentSelectedSubtree() && TestMaterialCreationIsUndoable() &&
                   TestGltfAssetIsSharedAcrossNodes() && TestUnusedResourcesAreRemovedAndUndoable() &&
                   TestNodeRenameIsUndoableAndRejectsEmptyNames() && TestCopyPasteSelectedSubtreeIsUndoable() &&
                   TestPrimitiveShapeEditIsUndoable() && TestSceneSettingsEditIsUndoable() ?
               0 :
               1;
}
