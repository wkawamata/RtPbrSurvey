#pragma once

#include "Scene/SceneDocument.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace App
{

class SceneEditorSession
{
public:
    explicit SceneEditorSession(RtPbrSurvey::SceneDocument document, bool modified = false);

    RtPbrSurvey::SceneDocument& Document();
    const RtPbrSurvey::SceneDocument& Document() const;

    const std::optional<std::string>& SelectedNodeId() const;
    RtPbrSurvey::SceneNode* SelectedNode();
    const RtPbrSurvey::SceneNode* SelectedNode() const;
    bool SelectNode(const std::string& nodeId);

    bool IsModified() const;
    void MarkSaved();
    void MarkModified();

    bool CanUndo() const;
    bool CanRedo() const;
    bool Undo();
    bool Redo();
    void BeginEdit();
    void CommitEdit();

    bool AddPrimitive(RtPbrSurvey::ScenePrimitiveKind kind, std::string* error = nullptr);
    bool AddEmpty(std::string* error = nullptr);
    bool AddGltfNode(const std::string& relativePath, std::string* error = nullptr);
    bool AddMaterial(std::string* materialId = nullptr, std::string* error = nullptr);
    bool RenameMaterial(const std::string& materialId, const std::string& name, std::string* error = nullptr);
    bool DuplicateMaterialForSelectedPrimitive(std::string* materialId = nullptr, std::string* error = nullptr);
    bool RemoveUnusedResources(size_t* removedAssetCount = nullptr,
                               size_t* removedMaterialCount = nullptr,
                               std::string* error = nullptr);
    bool RenameSelectedNode(const std::string& name, std::string* error = nullptr);
    bool CanPasteSubtree() const;
    bool CopySelectedSubtree(std::string* error = nullptr);
    bool PasteSubtree(std::string* error = nullptr);
    bool DeleteSelectedNode(std::string* error = nullptr);
    bool DuplicateSelectedSubtree(std::string* error = nullptr);
    bool ReparentSelectedNodePreservingWorld(const std::optional<std::string>& parentId,
                                             std::string* error = nullptr);

private:
    struct Snapshot
    {
        RtPbrSurvey::SceneDocument document;
        std::optional<std::string> selectedNodeId;
        bool modified = false;
    };

    struct Clipboard
    {
        std::vector<RtPbrSurvey::SceneNode> nodes;
        std::string rootId;
    };

    Snapshot CaptureSnapshot() const;
    void RestoreSnapshot(Snapshot snapshot);
    void PushUndoSnapshot();
    std::string CreateNodeId();
    std::string CreateAssetId() const;
    std::string CreateMaterialId() const;
    std::string CreateNodeName(RtPbrSurvey::ScenePrimitiveKind kind) const;
    std::string EnsurePrimitiveMaterial();

    RtPbrSurvey::SceneDocument m_document;
    std::optional<std::string> m_selectedNodeId;
    uint64_t m_nextNodeId = 1;
    bool m_modified = false;
    std::vector<Snapshot> m_undoHistory;
    std::vector<Snapshot> m_redoHistory;
    std::optional<Snapshot> m_activeEditSnapshot;
    std::optional<Clipboard> m_clipboard;

    static constexpr size_t kMaxHistoryEntries = 100;
};

} // namespace App
