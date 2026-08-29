//*********************************************************
//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
//*********************************************************

#pragma once

#include "RenderPassGraph.h"

#include <cstdint>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace Engine
{

struct RenderGraphDocumentId
{
    uint64_t value = 0;

    friend bool operator==(RenderGraphDocumentId lhs, RenderGraphDocumentId rhs)
    {
        return lhs.value == rhs.value;
    }
};

enum class RenderGraphNodeKind
{
    Pass,
    Resource,
};

enum class RenderGraphPinDirection
{
    Input,
    Output,
};

enum class RenderGraphResourceAccess
{
    Read,
    Write,
};

enum class RenderGraphResourceLifetimeKind
{
    Unknown,
    Transient,
    Persistent,
};

enum class RenderGraphResourceKind
{
    Unknown,
    Texture,
    Buffer,
};

enum class RenderGraphPingPongRole
{
    None,
    HistoryRead,
    CurrentWrite,
};

struct RenderGraphDocumentNode
{
    RenderGraphDocumentId id;
    RenderGraphNodeKind kind = RenderGraphNodeKind::Pass;
    std::string name;
    int passIndex = -1;
    int firstPass = -1;
    int lastPass = -1;
    RenderGraphResourceLifetimeKind lifetimeKind = RenderGraphResourceLifetimeKind::Unknown;
    RenderGraphResourceKind resourceKind = RenderGraphResourceKind::Unknown;
    RenderGraphDocumentId logicalGroupId;
    std::string logicalGroupName;
    int physicalIndex = -1;
    RenderGraphPingPongRole pingPongRole = RenderGraphPingPongRole::None;
};

struct RenderGraphDocumentPin
{
    RenderGraphDocumentId id;
    RenderGraphDocumentId nodeId;
    RenderGraphPinDirection direction = RenderGraphPinDirection::Input;
    RenderGraphResourceAccess access = RenderGraphResourceAccess::Read;
    D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;
};

struct RenderGraphDocumentLink
{
    RenderGraphDocumentId id;
    RenderGraphDocumentId fromPinId;
    RenderGraphDocumentId toPinId;
    RenderGraphDocumentId passNodeId;
    RenderGraphDocumentId resourceNodeId;
    RenderGraphResourceAccess access = RenderGraphResourceAccess::Read;
    D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;
};

struct RenderGraphResourceMetadata
{
    RenderGraphResourceLifetimeKind lifetimeKind = RenderGraphResourceLifetimeKind::Unknown;
    RenderGraphResourceKind resourceKind = RenderGraphResourceKind::Unknown;
    std::string logicalGroupName;
    int physicalIndex = -1;
    RenderGraphPingPongRole pingPongRole = RenderGraphPingPongRole::None;
};

using RenderGraphResourceMetadataMap = std::unordered_map<std::string, RenderGraphResourceMetadata>;

struct RenderGraphDocument
{
    std::vector<RenderGraphDocumentNode> nodes;
    std::vector<RenderGraphDocumentPin> pins;
    std::vector<RenderGraphDocumentLink> links;
};

enum class RenderGraphStateDiagnosticKind
{
    RequiredTransition,
    UavBarrierCandidate,
};

struct RenderGraphStateDiagnostic
{
    RenderGraphStateDiagnosticKind kind = RenderGraphStateDiagnosticKind::RequiredTransition;
    RenderGraphDocumentId resourceNodeId;
    RenderGraphDocumentId beforePassNodeId;
    RenderGraphDocumentId afterPassNodeId;
    D3D12_RESOURCE_STATES beforeState = D3D12_RESOURCE_STATE_COMMON;
    D3D12_RESOURCE_STATES afterState = D3D12_RESOURCE_STATE_COMMON;
};

enum class RenderGraphBarrierDiagnosticKind
{
    Missing,
    Unexpected,
    StateMismatch,
};

struct RenderGraphBarrierDiagnostic
{
    RenderGraphBarrierDiagnosticKind kind = RenderGraphBarrierDiagnosticKind::Missing;
    RenderGraphDocumentId resourceNodeId;
    RenderGraphDocumentId passNodeId;
    D3D12_RESOURCE_STATES expectedBeforeState = D3D12_RESOURCE_STATE_COMMON;
    D3D12_RESOURCE_STATES expectedAfterState = D3D12_RESOURCE_STATE_COMMON;
    D3D12_RESOURCE_STATES actualBeforeState = D3D12_RESOURCE_STATE_COMMON;
    D3D12_RESOURCE_STATES actualAfterState = D3D12_RESOURCE_STATE_COMMON;
};

struct RenderGraphSnapshotMetadata
{
    std::string label;
    std::string rendererMode;
    std::string sourceCommit;
    std::map<std::string, bool> features;
};

struct RenderGraphSnapshot
{
    static constexpr uint32_t kSchemaVersion = 1;
    uint32_t schemaVersion = kSchemaVersion;
    RenderGraphSnapshotMetadata metadata;
    RenderGraphDocument document;
};

struct RenderGraphDocumentDiff
{
    std::vector<RenderGraphDocumentId> addedNodes;
    std::vector<RenderGraphDocumentId> removedNodes;
    std::vector<RenderGraphDocumentId> changedNodes;
    std::vector<RenderGraphDocumentId> addedLinks;
    std::vector<RenderGraphDocumentId> removedLinks;
    std::vector<RenderGraphDocumentId> changedLinks;

    bool HasChanges() const;
};

enum class RenderGraphValidationSeverity
{
    Info,
    Warning,
    Error,
};

struct RenderGraphValidationMessage
{
    RenderGraphValidationSeverity severity = RenderGraphValidationSeverity::Warning;
    std::string code;
    std::string message;
    RenderGraphDocumentId nodeId;
    RenderGraphDocumentId passNodeId;
};

RenderGraphDocument BuildRenderGraphDocument(const std::vector<RenderPass>& renderPasses,
                                             const RenderGraphResourceMetadataMap& resourceMetadata = {});

std::string DumpRenderGraphDocumentText(const RenderGraphDocument& document);
std::string DumpRenderGraphDocumentDot(const RenderGraphDocument& document);
std::string FormatD3D12ResourceStates(D3D12_RESOURCE_STATES states);
std::vector<RenderGraphStateDiagnostic> BuildRenderGraphStateDiagnostics(const RenderGraphDocument& document);
std::vector<RenderGraphBarrierDiagnostic>
CompareRenderGraphBarrierEvents(const RenderGraphDocument& document,
                                const std::vector<RenderGraphBarrierEvent>& barrierEvents);
std::string SerializeRenderGraphSnapshot(const RenderGraphSnapshot& snapshot);
bool DeserializeRenderGraphSnapshot(const std::string& json,
                                    RenderGraphSnapshot& snapshot,
                                    std::string& error);
RenderGraphDocumentDiff DiffRenderGraphDocuments(const RenderGraphDocument& baseline,
                                                 const RenderGraphDocument& current);
std::vector<RenderGraphValidationMessage> ValidateRenderGraphDocument(const RenderGraphDocument& document);

struct RenderGraphAuthoringResource
{
    RenderGraphDocumentId id;
    std::string name;
    RenderGraphResourceLifetimeKind lifetimeKind = RenderGraphResourceLifetimeKind::Transient;
    RenderGraphResourceKind resourceKind = RenderGraphResourceKind::Texture;
};

struct RenderGraphAuthoringPass
{
    RenderGraphDocumentId id;
    std::string name;
};

struct RenderGraphAuthoringConnection
{
    RenderGraphDocumentId id;
    RenderGraphDocumentId passId;
    RenderGraphDocumentId resourceId;
    RenderGraphResourceAccess access = RenderGraphResourceAccess::Read;
    D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;
};

struct RenderGraphAuthoringDocument
{
    static constexpr uint32_t kSchemaVersion = 1;
    uint32_t schemaVersion = kSchemaVersion;
    std::vector<RenderGraphAuthoringPass> passes;
    std::vector<RenderGraphAuthoringResource> resources;
    std::vector<RenderGraphAuthoringConnection> connections;
};

enum class RenderGraphEditCommandKind
{
    AddPass,
    RemovePass,
    AddResource,
    RemoveResource,
    ConnectResource,
    DisconnectResource,
    MovePass,
};

struct RenderGraphEditCommand
{
    RenderGraphEditCommandKind kind = RenderGraphEditCommandKind::AddPass;
    RenderGraphDocumentId targetId;
    int index = -1;
    RenderGraphAuthoringPass pass;
    RenderGraphAuthoringResource resource;
    RenderGraphAuthoringConnection connection;
};

std::vector<RenderGraphValidationMessage>
ValidateRenderGraphAuthoringDocument(const RenderGraphAuthoringDocument& document);
std::string SerializeRenderGraphAuthoringDocument(const RenderGraphAuthoringDocument& document);
bool DeserializeRenderGraphAuthoringDocument(const std::string& json,
                                             RenderGraphAuthoringDocument& document,
                                             std::string& error);
RenderGraphDocument BuildRenderGraphAuthoringPreview(const RenderGraphAuthoringDocument& document);

class RenderGraphEditHistory
{
public:
    explicit RenderGraphEditHistory(RenderGraphAuthoringDocument document = {});

    const RenderGraphAuthoringDocument& Document() const;
    bool Apply(const RenderGraphEditCommand& command, std::string& error);
    bool CanUndo() const;
    bool CanRedo() const;
    bool Undo();
    bool Redo();

private:
    RenderGraphAuthoringDocument m_document;
    std::vector<RenderGraphAuthoringDocument> m_undo;
    std::vector<RenderGraphAuthoringDocument> m_redo;
};

} // namespace Engine

namespace std
{
template <> struct hash<Engine::RenderGraphDocumentId>
{
    size_t operator()(Engine::RenderGraphDocumentId id) const noexcept
    {
        return hash<uint64_t>{}(id.value);
    }
};
} // namespace std
