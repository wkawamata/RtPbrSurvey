#include "stdafx.h"

#include "App/SceneEditorSession.h"

#include "Scene/SceneGraph.h"

#include <algorithm>
#include <filesystem>
#include <iterator>
#include <unordered_map>
#include <unordered_set>

namespace App
{
namespace
{
const char* PrimitiveName(RtPbrSurvey::ScenePrimitiveKind kind)
{
    switch (kind)
    {
    case RtPbrSurvey::ScenePrimitiveKind::Cube:
        return "Cube";
    case RtPbrSurvey::ScenePrimitiveKind::Sphere:
        return "Sphere";
    case RtPbrSurvey::ScenePrimitiveKind::Plane:
        return "Plane";
    case RtPbrSurvey::ScenePrimitiveKind::Cylinder:
        return "Cylinder";
    }
    return "Primitive";
}
} // namespace

SceneEditorSession::SceneEditorSession(RtPbrSurvey::SceneDocument document, bool modified)
    : m_document(std::move(document))
    , m_modified(modified)
{
}

RtPbrSurvey::SceneDocument& SceneEditorSession::Document()
{
    return m_document;
}

const RtPbrSurvey::SceneDocument& SceneEditorSession::Document() const
{
    return m_document;
}

const std::optional<std::string>& SceneEditorSession::SelectedNodeId() const
{
    return m_selectedNodeId;
}

RtPbrSurvey::SceneNode* SceneEditorSession::SelectedNode()
{
    if (!m_selectedNodeId.has_value())
    {
        return nullptr;
    }
    const auto node = std::find_if(m_document.nodes.begin(), m_document.nodes.end(), [this](const RtPbrSurvey::SceneNode& candidate)
    {
        return candidate.id == *m_selectedNodeId;
    });
    return node == m_document.nodes.end() ? nullptr : &*node;
}

const RtPbrSurvey::SceneNode* SceneEditorSession::SelectedNode() const
{
    return const_cast<SceneEditorSession*>(this)->SelectedNode();
}

bool SceneEditorSession::SelectNode(const std::string& nodeId)
{
    const auto node = std::find_if(m_document.nodes.begin(), m_document.nodes.end(), [&nodeId](const RtPbrSurvey::SceneNode& candidate)
    {
        return candidate.id == nodeId;
    });
    if (node == m_document.nodes.end())
    {
        return false;
    }
    m_selectedNodeId = nodeId;
    return true;
}

bool SceneEditorSession::IsModified() const
{
    return m_modified;
}

void SceneEditorSession::MarkSaved()
{
    m_modified = false;
}

void SceneEditorSession::MarkModified()
{
    m_modified = true;
}

bool SceneEditorSession::CanUndo() const
{
    return !m_undoHistory.empty();
}

bool SceneEditorSession::CanRedo() const
{
    return !m_redoHistory.empty();
}

bool SceneEditorSession::Undo()
{
    if (m_undoHistory.empty())
    {
        return false;
    }
    m_redoHistory.push_back(CaptureSnapshot());
    RestoreSnapshot(std::move(m_undoHistory.back()));
    m_undoHistory.pop_back();
    return true;
}

bool SceneEditorSession::Redo()
{
    if (m_redoHistory.empty())
    {
        return false;
    }
    m_undoHistory.push_back(CaptureSnapshot());
    RestoreSnapshot(std::move(m_redoHistory.back()));
    m_redoHistory.pop_back();
    return true;
}

void SceneEditorSession::BeginEdit()
{
    if (!m_activeEditSnapshot.has_value())
    {
        m_activeEditSnapshot = CaptureSnapshot();
    }
}

void SceneEditorSession::CommitEdit()
{
    if (!m_activeEditSnapshot.has_value())
    {
        return;
    }
    m_undoHistory.push_back(std::move(*m_activeEditSnapshot));
    m_activeEditSnapshot.reset();
    if (m_undoHistory.size() > kMaxHistoryEntries)
    {
        m_undoHistory.erase(m_undoHistory.begin());
    }
    m_redoHistory.clear();
    m_modified = true;
}

bool SceneEditorSession::AddPrimitive(RtPbrSurvey::ScenePrimitiveKind kind, std::string* error)
{
    PushUndoSnapshot();
    RtPbrSurvey::SceneNode node;
    node.id = CreateNodeId();
    node.name = CreateNodeName(kind);
    node.type = RtPbrSurvey::SceneNodeType::Primitive;
    node.primitive.kind = kind;
    node.materialId = EnsurePrimitiveMaterial();
    if (kind == RtPbrSurvey::ScenePrimitiveKind::Plane)
    {
        node.primitive.width = 5.0f;
        node.primitive.depth = 5.0f;
    }
    m_document.nodes.push_back(std::move(node));
    m_selectedNodeId = m_document.nodes.back().id;
    m_modified = true;
    if (error != nullptr)
    {
        error->clear();
    }
    return true;
}

bool SceneEditorSession::AddEmpty(std::string* error)
{
    PushUndoSnapshot();
    RtPbrSurvey::SceneNode node;
    node.id = CreateNodeId();
    node.name = "Empty";
    std::unordered_set<std::string> names;
    for (const RtPbrSurvey::SceneNode& existing : m_document.nodes)
    {
        names.insert(existing.name);
    }
    for (uint64_t suffix = 2; names.contains(node.name); ++suffix)
    {
        node.name = "Empty " + std::to_string(suffix);
    }
    m_document.nodes.push_back(std::move(node));
    m_selectedNodeId = m_document.nodes.back().id;
    m_modified = true;
    if (error != nullptr)
    {
        error->clear();
    }
    return true;
}

bool SceneEditorSession::AddGltfNode(const std::string& relativePath, std::string* error)
{
    const std::filesystem::path assetPath(relativePath);
    if (assetPath.empty() || assetPath.is_absolute())
    {
        if (error != nullptr)
        {
            *error = "glTF asset path must be non-empty and relative.";
        }
        return false;
    }

    const std::string normalizedPath = assetPath.lexically_normal().generic_string();
    auto asset = std::find_if(m_document.assets.begin(), m_document.assets.end(), [&normalizedPath](const RtPbrSurvey::SceneAsset& candidate)
    {
        return candidate.path == normalizedPath;
    });

    PushUndoSnapshot();
    if (asset == m_document.assets.end())
    {
        RtPbrSurvey::SceneAsset newAsset;
        newAsset.id = CreateAssetId();
        newAsset.path = normalizedPath;
        m_document.assets.push_back(std::move(newAsset));
        asset = std::prev(m_document.assets.end());
    }

    RtPbrSurvey::SceneNode node;
    node.id = CreateNodeId();
    node.name = assetPath.stem().generic_string();
    if (node.name.empty())
    {
        node.name = "glTF";
    }
    node.type = RtPbrSurvey::SceneNodeType::Gltf;
    node.assetId = asset->id;
    m_document.nodes.push_back(std::move(node));
    m_selectedNodeId = m_document.nodes.back().id;
    m_modified = true;
    if (error != nullptr)
    {
        error->clear();
    }
    return true;
}

bool SceneEditorSession::AddMaterial(std::string* materialId, std::string* error)
{
    PushUndoSnapshot();
    RtPbrSurvey::SceneMaterial material;
    material.id = CreateMaterialId();
    material.name = "Material " + std::to_string(m_document.materials.size() + 1);
    m_document.materials.push_back(material);
    m_modified = true;
    if (materialId != nullptr)
    {
        *materialId = material.id;
    }
    if (error != nullptr)
    {
        error->clear();
    }
    return true;
}

bool SceneEditorSession::RemoveUnusedResources(size_t* removedAssetCount,
                                                size_t* removedMaterialCount,
                                                std::string* error)
{
    std::unordered_set<std::string> usedAssetIds;
    std::unordered_set<std::string> usedMaterialIds;
    for (const RtPbrSurvey::SceneNode& node : m_document.nodes)
    {
        if (node.type == RtPbrSurvey::SceneNodeType::Gltf)
        {
            usedAssetIds.insert(node.assetId);
        }
        else if (node.type == RtPbrSurvey::SceneNodeType::Primitive)
        {
            usedMaterialIds.insert(node.materialId);
        }
    }

    RtPbrSurvey::SceneDocument candidate = m_document;
    const size_t assetCount = candidate.assets.size();
    candidate.assets.erase(std::remove_if(candidate.assets.begin(), candidate.assets.end(), [&usedAssetIds](const RtPbrSurvey::SceneAsset& asset)
    {
        return !usedAssetIds.contains(asset.id);
    }), candidate.assets.end());
    const size_t materialCount = candidate.materials.size();
    candidate.materials.erase(std::remove_if(candidate.materials.begin(), candidate.materials.end(), [&usedMaterialIds](const RtPbrSurvey::SceneMaterial& material)
    {
        return !usedMaterialIds.contains(material.id);
    }), candidate.materials.end());

    const size_t assetsRemoved = assetCount - candidate.assets.size();
    const size_t materialsRemoved = materialCount - candidate.materials.size();
    if (assetsRemoved == 0 && materialsRemoved == 0)
    {
        if (removedAssetCount != nullptr)
        {
            *removedAssetCount = 0;
        }
        if (removedMaterialCount != nullptr)
        {
            *removedMaterialCount = 0;
        }
        if (error != nullptr)
        {
            error->clear();
        }
        return false;
    }

    PushUndoSnapshot();
    m_document = std::move(candidate);
    m_modified = true;
    if (removedAssetCount != nullptr)
    {
        *removedAssetCount = assetsRemoved;
    }
    if (removedMaterialCount != nullptr)
    {
        *removedMaterialCount = materialsRemoved;
    }
    if (error != nullptr)
    {
        error->clear();
    }
    return true;
}

bool SceneEditorSession::DeleteSelectedNode(std::string* error)
{
    if (!m_selectedNodeId.has_value())
    {
        if (error != nullptr)
        {
            *error = "No hierarchy node is selected.";
        }
        return false;
    }

    PushUndoSnapshot();

    std::unordered_set<std::string> removedIds = {*m_selectedNodeId};
    bool addedDescendant = true;
    while (addedDescendant)
    {
        addedDescendant = false;
        for (const RtPbrSurvey::SceneNode& node : m_document.nodes)
        {
            if (node.parentId.has_value() && removedIds.contains(*node.parentId))
            {
                addedDescendant = removedIds.insert(node.id).second || addedDescendant;
            }
        }
    }

    m_document.nodes.erase(std::remove_if(m_document.nodes.begin(), m_document.nodes.end(), [&removedIds](const RtPbrSurvey::SceneNode& node)
    {
        return removedIds.contains(node.id);
    }), m_document.nodes.end());
    m_selectedNodeId.reset();
    m_modified = true;
    if (error != nullptr)
    {
        error->clear();
    }
    return true;
}

bool SceneEditorSession::DuplicateSelectedSubtree(std::string* error)
{
    const RtPbrSurvey::SceneNode* selectedNode = SelectedNode();
    if (selectedNode == nullptr)
    {
        if (error != nullptr)
        {
            *error = "No hierarchy node is selected.";
        }
        return false;
    }

    std::unordered_set<std::string> sourceIds = {selectedNode->id};
    bool addedDescendant = true;
    while (addedDescendant)
    {
        addedDescendant = false;
        for (const RtPbrSurvey::SceneNode& node : m_document.nodes)
        {
            if (node.parentId.has_value() && sourceIds.contains(*node.parentId))
            {
                addedDescendant = sourceIds.insert(node.id).second || addedDescendant;
            }
        }
    }

    PushUndoSnapshot();
    std::unordered_map<std::string, std::string> duplicateIds;
    std::vector<RtPbrSurvey::SceneNode> duplicates;
    duplicates.reserve(sourceIds.size());
    for (const RtPbrSurvey::SceneNode& node : m_document.nodes)
    {
        if (!sourceIds.contains(node.id))
        {
            continue;
        }
        RtPbrSurvey::SceneNode duplicate = node;
        const std::string duplicateId = CreateNodeId();
        duplicateIds.emplace(node.id, duplicateId);
        duplicate.id = duplicateId;
        if (node.id == selectedNode->id)
        {
            duplicate.name += " Copy";
        }
        duplicates.push_back(std::move(duplicate));
    }
    for (RtPbrSurvey::SceneNode& duplicate : duplicates)
    {
        if (duplicate.parentId.has_value())
        {
            const auto parent = duplicateIds.find(*duplicate.parentId);
            if (parent != duplicateIds.end())
            {
                duplicate.parentId = parent->second;
            }
        }
    }
    const std::string duplicatedRootId = duplicateIds.at(selectedNode->id);
    m_document.nodes.insert(m_document.nodes.end(),
                            std::make_move_iterator(duplicates.begin()),
                            std::make_move_iterator(duplicates.end()));
    m_selectedNodeId = duplicatedRootId;
    m_modified = true;
    if (error != nullptr)
    {
        error->clear();
    }
    return true;
}

bool SceneEditorSession::ReparentSelectedNodePreservingWorld(const std::optional<std::string>& parentId,
                                                              std::string* error)
{
    if (!m_selectedNodeId.has_value())
    {
        if (error != nullptr)
        {
            *error = "No hierarchy node is selected.";
        }
        return false;
    }
    if (SelectedNode()->parentId == parentId)
    {
        if (error != nullptr)
        {
            error->clear();
        }
        return true;
    }

    RtPbrSurvey::SceneDocument candidate = m_document;
    if (!RtPbrSurvey::ReparentSceneNodePreservingWorld(candidate, *m_selectedNodeId, parentId, error))
    {
        return false;
    }
    PushUndoSnapshot();
    m_document = std::move(candidate);
    m_modified = true;
    if (error != nullptr)
    {
        error->clear();
    }
    return true;
}

SceneEditorSession::Snapshot SceneEditorSession::CaptureSnapshot() const
{
    return {m_document, m_selectedNodeId, m_modified};
}

void SceneEditorSession::RestoreSnapshot(Snapshot snapshot)
{
    m_document = std::move(snapshot.document);
    m_selectedNodeId = std::move(snapshot.selectedNodeId);
    m_modified = snapshot.modified;
    m_activeEditSnapshot.reset();
}

void SceneEditorSession::PushUndoSnapshot()
{
    m_undoHistory.push_back(CaptureSnapshot());
    if (m_undoHistory.size() > kMaxHistoryEntries)
    {
        m_undoHistory.erase(m_undoHistory.begin());
    }
    m_redoHistory.clear();
}

std::string SceneEditorSession::CreateNodeId()
{
    std::unordered_set<std::string> ids;
    for (const RtPbrSurvey::SceneNode& node : m_document.nodes)
    {
        ids.insert(node.id);
    }
    std::string id;
    do
    {
        id = "node-" + std::to_string(m_nextNodeId++);
    } while (ids.contains(id));
    return id;
}

std::string SceneEditorSession::CreateAssetId() const
{
    std::unordered_set<std::string> ids;
    for (const RtPbrSurvey::SceneAsset& asset : m_document.assets)
    {
        ids.insert(asset.id);
    }
    for (uint64_t suffix = 1;; ++suffix)
    {
        const std::string id = "asset-" + std::to_string(suffix);
        if (!ids.contains(id))
        {
            return id;
        }
    }
}

std::string SceneEditorSession::CreateMaterialId() const
{
    std::unordered_set<std::string> ids;
    for (const RtPbrSurvey::SceneMaterial& material : m_document.materials)
    {
        ids.insert(material.id);
    }
    for (uint64_t suffix = 1;; ++suffix)
    {
        const std::string id = "material-" + std::to_string(suffix);
        if (!ids.contains(id))
        {
            return id;
        }
    }
}

std::string SceneEditorSession::CreateNodeName(RtPbrSurvey::ScenePrimitiveKind kind) const
{
    const std::string baseName = PrimitiveName(kind);
    std::unordered_set<std::string> names;
    for (const RtPbrSurvey::SceneNode& node : m_document.nodes)
    {
        names.insert(node.name);
    }
    if (!names.contains(baseName))
    {
        return baseName;
    }
    for (uint64_t suffix = 2;; ++suffix)
    {
        const std::string candidate = baseName + " " + std::to_string(suffix);
        if (!names.contains(candidate))
        {
            return candidate;
        }
    }
}

std::string SceneEditorSession::EnsurePrimitiveMaterial()
{
    if (!m_document.materials.empty())
    {
        return m_document.materials.front().id;
    }

    RtPbrSurvey::SceneMaterial material;
    material.id = "material-default";
    material.name = "Default Material";
    m_document.materials.push_back(std::move(material));
    return m_document.materials.back().id;
}

} // namespace App
