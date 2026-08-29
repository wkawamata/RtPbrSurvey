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

#include "stdafx.h"

#include "RenderGraphDocument.h"

#include "RenderPassResources.h"

#include <algorithm>
#include <array>
#include <iomanip>
#include <sstream>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

#include <nlohmann/json.hpp>

namespace Engine
{
namespace
{
uint64_t HashDocumentId(std::string_view value)
{
    constexpr uint64_t offsetBasis = 14695981039346656037ull;
    constexpr uint64_t prime = 1099511628211ull;
    uint64_t hash = offsetBasis;
    for (const unsigned char character : value)
    {
        hash ^= character;
        hash *= prime;
    }
    return hash == 0 ? 1 : hash;
}

RenderGraphDocumentId MakeDocumentId(std::string_view category, std::string_view value)
{
    std::string key(category);
    key.push_back(':');
    key.append(value);
    return {HashDocumentId(key)};
}

std::string Narrow(const wchar_t* value)
{
    if (value == nullptr)
    {
        return {};
    }

    const int length = WideCharToMultiByte(CP_UTF8, 0, value, -1, nullptr, 0, nullptr, nullptr);
    if (length <= 1)
    {
        return {};
    }

    std::string result(static_cast<size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value, -1, result.data(), length, nullptr, nullptr);
    result.pop_back();
    return result;
}

std::string EscapeDot(std::string_view value)
{
    std::string escaped;
    escaped.reserve(value.size());
    for (const char character : value)
    {
        switch (character)
        {
            case '\\':
                escaped += "\\\\";
                break;
            case '"':
                escaped += "\\\"";
                break;
            case '\n':
                escaped += "\\n";
                break;
            case '\r':
                break;
            default:
                escaped.push_back(character);
                break;
        }
    }
    return escaped;
}

const char* LifetimeKindName(RenderGraphResourceLifetimeKind kind)
{
    switch (kind)
    {
        case RenderGraphResourceLifetimeKind::Transient:
            return "transient";
        case RenderGraphResourceLifetimeKind::Persistent:
            return "persistent";
        default:
            return "unknown";
    }
}

const char* ResourceKindName(RenderGraphResourceKind kind)
{
    switch (kind)
    {
        case RenderGraphResourceKind::Texture:
            return "texture";
        case RenderGraphResourceKind::Buffer:
            return "buffer";
        default:
            return "unknown";
    }
}

const RenderGraphDocumentNode* FindNode(const RenderGraphDocument& document, RenderGraphDocumentId id)
{
    const auto node = std::find_if(document.nodes.begin(),
                                   document.nodes.end(),
                                   [id](const RenderGraphDocumentNode& value) { return value.id == id; });
    return node != document.nodes.end() ? &*node : nullptr;
}

void AddUsage(RenderGraphDocument& document,
              const RenderGraphDocumentNode& passNode,
              const RenderGraphDocumentNode& resourceNode,
              const ResourceUsage& usage,
              RenderGraphResourceAccess access,
              size_t usageIndex)
{
    const std::string identity = std::to_string(passNode.id.value) + ":" + std::to_string(resourceNode.id.value) + ":" +
                                 std::to_string(static_cast<int>(access)) + ":" + std::to_string(usageIndex);
    const RenderGraphDocumentId passPinId = MakeDocumentId("pass-pin", identity);
    const RenderGraphDocumentId resourcePinId = MakeDocumentId("resource-pin", identity);
    const RenderGraphDocumentId linkId = MakeDocumentId("link", identity);

    const bool read = access == RenderGraphResourceAccess::Read;
    document.pins.push_back({passPinId,
                             passNode.id,
                             read ? RenderGraphPinDirection::Input : RenderGraphPinDirection::Output,
                             access,
                             usage.state});
    document.pins.push_back({resourcePinId,
                             resourceNode.id,
                             read ? RenderGraphPinDirection::Output : RenderGraphPinDirection::Input,
                             access,
                             usage.state});
    document.links.push_back({linkId,
                              read ? resourcePinId : passPinId,
                              read ? passPinId : resourcePinId,
                              passNode.id,
                              resourceNode.id,
                              access,
                              usage.state});
}
} // namespace

RenderGraphDocument BuildRenderGraphDocument(const std::vector<RenderPass>& renderPasses,
                                             const RenderGraphResourceMetadataMap& resourceMetadata)
{
    RenderGraphDocument document;
    const ResourceLifetimeMap lifetimes = AnalyzeResourceLifetimes(renderPasses);

    std::vector<std::string> resourceNames;
    resourceNames.reserve(lifetimes.size());
    for (const auto& [name, lifetime] : lifetimes)
    {
        (void)lifetime;
        resourceNames.push_back(name);
    }
    std::sort(resourceNames.begin(), resourceNames.end());

    std::unordered_map<std::string, size_t> resourceNodeIndices;
    for (const std::string& name : resourceNames)
    {
        const ResourceLifetime& lifetime = lifetimes.at(name);
        const auto metadata = resourceMetadata.find(name);
        const RenderGraphResourceLifetimeKind lifetimeKind = metadata != resourceMetadata.end()
                                                                 ? metadata->second.lifetimeKind
                                                                 : RenderGraphResourceLifetimeKind::Unknown;
        const RenderGraphResourceKind resourceKind =
            metadata != resourceMetadata.end() ? metadata->second.resourceKind : RenderGraphResourceKind::Unknown;
        const std::string logicalGroupName =
            metadata != resourceMetadata.end() ? metadata->second.logicalGroupName : std::string{};
        const int physicalIndex = metadata != resourceMetadata.end() ? metadata->second.physicalIndex : -1;
        const RenderGraphPingPongRole pingPongRole =
            metadata != resourceMetadata.end() ? metadata->second.pingPongRole : RenderGraphPingPongRole::None;
        resourceNodeIndices[name] = document.nodes.size();
        document.nodes.push_back({MakeDocumentId("resource", name),
                                  RenderGraphNodeKind::Resource,
                                  name,
                                  -1,
                                  lifetime.firstPass,
                                  lifetime.lastPass,
                                  lifetimeKind,
                                  resourceKind,
                                  logicalGroupName.empty() ? RenderGraphDocumentId{}
                                                           : MakeDocumentId("resource-group", logicalGroupName),
                                  logicalGroupName,
                                  physicalIndex,
                                  pingPongRole});
    }

    std::unordered_map<std::string, size_t> passNameOccurrences;
    for (size_t passIndex = 0; passIndex < renderPasses.size(); ++passIndex)
    {
        const RenderPass& pass = renderPasses[passIndex];
        const std::string passName = Narrow(pass.name);
        const size_t occurrence = passNameOccurrences[passName]++;
        const std::string passIdentity = passName + ":" + std::to_string(occurrence);
        const size_t nodeIndex = document.nodes.size();
        document.nodes.push_back(
            {MakeDocumentId("pass", passIdentity), RenderGraphNodeKind::Pass, passName, static_cast<int>(passIndex)});
        const RenderGraphDocumentNode passNode = document.nodes[nodeIndex];

        for (size_t usageIndex = 0; usageIndex < pass.reads.size(); ++usageIndex)
        {
            const ResourceUsage& usage = pass.reads[usageIndex];
            AddUsage(document,
                     passNode,
                     document.nodes[resourceNodeIndices.at(usage.name)],
                     usage,
                     RenderGraphResourceAccess::Read,
                     usageIndex);
        }
        for (size_t usageIndex = 0; usageIndex < pass.writes.size(); ++usageIndex)
        {
            const ResourceUsage& usage = pass.writes[usageIndex];
            AddUsage(document,
                     passNode,
                     document.nodes[resourceNodeIndices.at(usage.name)],
                     usage,
                     RenderGraphResourceAccess::Write,
                     usageIndex);
        }
    }

    return document;
}

std::string FormatD3D12ResourceStates(D3D12_RESOURCE_STATES states)
{
    struct StateName
    {
        D3D12_RESOURCE_STATES state;
        const char* name;
    };
    constexpr std::array stateNames = {
        StateName{D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER, "VERTEX_AND_CONSTANT_BUFFER"},
        StateName{D3D12_RESOURCE_STATE_INDEX_BUFFER, "INDEX_BUFFER"},
        StateName{D3D12_RESOURCE_STATE_RENDER_TARGET, "RENDER_TARGET"},
        StateName{D3D12_RESOURCE_STATE_UNORDERED_ACCESS, "UNORDERED_ACCESS"},
        StateName{D3D12_RESOURCE_STATE_DEPTH_WRITE, "DEPTH_WRITE"},
        StateName{D3D12_RESOURCE_STATE_DEPTH_READ, "DEPTH_READ"},
        StateName{D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, "NON_PIXEL_SHADER_RESOURCE"},
        StateName{D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, "PIXEL_SHADER_RESOURCE"},
        StateName{D3D12_RESOURCE_STATE_STREAM_OUT, "STREAM_OUT"},
        StateName{D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT, "INDIRECT_ARGUMENT"},
        StateName{D3D12_RESOURCE_STATE_COPY_DEST, "COPY_DEST"},
        StateName{D3D12_RESOURCE_STATE_COPY_SOURCE, "COPY_SOURCE"},
        StateName{D3D12_RESOURCE_STATE_RESOLVE_DEST, "RESOLVE_DEST"},
        StateName{D3D12_RESOURCE_STATE_RESOLVE_SOURCE, "RESOLVE_SOURCE"},
        StateName{D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, "RAYTRACING_ACCELERATION_STRUCTURE"},
        StateName{D3D12_RESOURCE_STATE_SHADING_RATE_SOURCE, "SHADING_RATE_SOURCE"},
    };

    if (states == D3D12_RESOURCE_STATE_COMMON)
    {
        return "COMMON";
    }
    if (states == D3D12_RESOURCE_STATE_PRESENT)
    {
        return "PRESENT";
    }
    if (states == D3D12_RESOURCE_STATE_GENERIC_READ)
    {
        return "GENERIC_READ";
    }

    std::ostringstream stream;
    D3D12_RESOURCE_STATES remaining = states;
    bool first = true;
    for (const StateName& stateName : stateNames)
    {
        if ((remaining & stateName.state) == 0)
        {
            continue;
        }
        if (!first)
        {
            stream << '|';
        }
        stream << stateName.name;
        first = false;
        remaining = static_cast<D3D12_RESOURCE_STATES>(remaining & ~stateName.state);
    }
    if (remaining != 0 || first)
    {
        if (!first)
        {
            stream << '|';
        }
        stream << "0x" << std::uppercase << std::hex << static_cast<unsigned int>(remaining);
    }
    return stream.str();
}

std::vector<RenderGraphStateDiagnostic> BuildRenderGraphStateDiagnostics(const RenderGraphDocument& document)
{
    std::unordered_map<RenderGraphDocumentId, int> passIndices;
    for (const RenderGraphDocumentNode& node : document.nodes)
    {
        if (node.kind == RenderGraphNodeKind::Pass)
        {
            passIndices[node.id] = node.passIndex;
        }
    }

    std::vector<const RenderGraphDocumentLink*> usages;
    usages.reserve(document.links.size());
    for (const RenderGraphDocumentLink& link : document.links)
    {
        usages.push_back(&link);
    }
    std::sort(usages.begin(),
              usages.end(),
              [&passIndices](const auto* lhs, const auto* rhs)
              {
                  if (lhs->resourceNodeId != rhs->resourceNodeId)
                  {
                      return lhs->resourceNodeId.value < rhs->resourceNodeId.value;
                  }
                  const int lhsPassIndex = passIndices.at(lhs->passNodeId);
                  const int rhsPassIndex = passIndices.at(rhs->passNodeId);
                  if (lhsPassIndex != rhsPassIndex)
                  {
                      return lhsPassIndex < rhsPassIndex;
                  }
                  if (lhs->access != rhs->access)
                  {
                      return lhs->access < rhs->access;
                  }
                  return lhs->id.value < rhs->id.value;
              });

    std::vector<RenderGraphStateDiagnostic> diagnostics;
    const RenderGraphDocumentLink* previous = nullptr;
    for (const RenderGraphDocumentLink* usage : usages)
    {
        if (previous == nullptr || previous->resourceNodeId != usage->resourceNodeId)
        {
            previous = usage;
            continue;
        }

        if (previous->state != usage->state)
        {
            diagnostics.push_back({RenderGraphStateDiagnosticKind::RequiredTransition,
                                   usage->resourceNodeId,
                                   previous->passNodeId,
                                   usage->passNodeId,
                                   previous->state,
                                   usage->state});
        }
        else if (usage->state == D3D12_RESOURCE_STATE_UNORDERED_ACCESS &&
                 (previous->access == RenderGraphResourceAccess::Write ||
                  usage->access == RenderGraphResourceAccess::Write))
        {
            diagnostics.push_back({RenderGraphStateDiagnosticKind::UavBarrierCandidate,
                                   usage->resourceNodeId,
                                   previous->passNodeId,
                                   usage->passNodeId,
                                   previous->state,
                                   usage->state});
        }
        previous = usage;
    }
    std::sort(diagnostics.begin(),
              diagnostics.end(),
              [&passIndices](const auto& lhs, const auto& rhs)
              {
                  const int lhsPassIndex = passIndices.at(lhs.afterPassNodeId);
                  const int rhsPassIndex = passIndices.at(rhs.afterPassNodeId);
                  return lhsPassIndex != rhsPassIndex ? lhsPassIndex < rhsPassIndex
                                                      : lhs.resourceNodeId.value < rhs.resourceNodeId.value;
              });
    return diagnostics;
}

std::string DumpRenderGraphDocumentText(const RenderGraphDocument& document)
{
    std::ostringstream stream;
    for (const RenderGraphDocumentNode& pass : document.nodes)
    {
        if (pass.kind != RenderGraphNodeKind::Pass)
        {
            continue;
        }
        stream << "Pass [" << pass.passIndex << "] " << pass.name << '\n';
        for (const RenderGraphDocumentLink& link : document.links)
        {
            if (link.passNodeId != pass.id)
            {
                continue;
            }
            const RenderGraphDocumentNode* resource = FindNode(document, link.resourceNodeId);
            if (resource == nullptr)
            {
                continue;
            }
            stream << "  " << (link.access == RenderGraphResourceAccess::Read ? "Read " : "Write ") << resource->name
                   << " state=" << FormatD3D12ResourceStates(link.state) << " lifetime=[" << resource->firstPass << ','
                   << resource->lastPass << "] kind=" << LifetimeKindName(resource->lifetimeKind)
                   << " type=" << ResourceKindName(resource->resourceKind) << '\n';
        }
    }
    return stream.str();
}

std::string DumpRenderGraphDocumentDot(const RenderGraphDocument& document)
{
    std::ostringstream stream;
    stream << "digraph RenderGraph {\n"
           << "  rankdir=LR;\n";
    for (const RenderGraphDocumentNode& node : document.nodes)
    {
        stream << "  n" << node.id.value << " [shape=" << (node.kind == RenderGraphNodeKind::Pass ? "box" : "ellipse")
               << ", label=\"" << EscapeDot(node.name);
        if (node.kind == RenderGraphNodeKind::Resource)
        {
            stream << "\\ntype=" << ResourceKindName(node.resourceKind) << "\\nlifetime=[" << node.firstPass << ','
                   << node.lastPass << "]\\n"
                   << LifetimeKindName(node.lifetimeKind);
        }
        stream << "\"];\n";
    }
    for (const RenderGraphDocumentLink& link : document.links)
    {
        const RenderGraphDocumentId fromNode =
            link.access == RenderGraphResourceAccess::Read ? link.resourceNodeId : link.passNodeId;
        const RenderGraphDocumentId toNode =
            link.access == RenderGraphResourceAccess::Read ? link.passNodeId : link.resourceNodeId;
        stream << "  n" << fromNode.value << " -> n" << toNode.value << " [label=\""
               << (link.access == RenderGraphResourceAccess::Read ? "read " : "write ")
               << EscapeDot(FormatD3D12ResourceStates(link.state)) << "\"];\n";
    }
    stream << "}\n";
    return stream.str();
}

namespace
{
using Json = nlohmann::ordered_json;

std::string IdToHex(RenderGraphDocumentId id)
{
    std::ostringstream stream;
    stream << std::hex << std::setw(16) << std::setfill('0') << id.value;
    return stream.str();
}

bool HexToId(const Json& value, RenderGraphDocumentId& id)
{
    if (!value.is_string())
    {
        return false;
    }
    std::istringstream stream(value.get<std::string>());
    stream >> std::hex >> id.value;
    return !stream.fail() && stream.eof() && id.value != 0;
}

template <typename ValueT> std::vector<const ValueT*> SortedById(const std::vector<ValueT>& values)
{
    std::vector<const ValueT*> sorted;
    sorted.reserve(values.size());
    for (const ValueT& value : values)
    {
        sorted.push_back(&value);
    }
    std::sort(sorted.begin(), sorted.end(), [](const ValueT* lhs, const ValueT* rhs) {
        return lhs->id.value < rhs->id.value;
    });
    return sorted;
}

bool NodesEqual(const RenderGraphDocumentNode& lhs, const RenderGraphDocumentNode& rhs)
{
    return lhs.kind == rhs.kind && lhs.name == rhs.name && lhs.passIndex == rhs.passIndex &&
           lhs.firstPass == rhs.firstPass && lhs.lastPass == rhs.lastPass && lhs.lifetimeKind == rhs.lifetimeKind &&
           lhs.resourceKind == rhs.resourceKind && lhs.logicalGroupId == rhs.logicalGroupId &&
           lhs.logicalGroupName == rhs.logicalGroupName && lhs.physicalIndex == rhs.physicalIndex &&
           lhs.pingPongRole == rhs.pingPongRole;
}

bool LinksEqual(const RenderGraphDocumentLink& lhs, const RenderGraphDocumentLink& rhs)
{
    return lhs.fromPinId == rhs.fromPinId && lhs.toPinId == rhs.toPinId && lhs.passNodeId == rhs.passNodeId &&
           lhs.resourceNodeId == rhs.resourceNodeId && lhs.access == rhs.access && lhs.state == rhs.state;
}
} // namespace

bool RenderGraphDocumentDiff::HasChanges() const
{
    return !addedNodes.empty() || !removedNodes.empty() || !changedNodes.empty() || !addedLinks.empty() ||
           !removedLinks.empty() || !changedLinks.empty();
}

std::string SerializeRenderGraphSnapshot(const RenderGraphSnapshot& snapshot)
{
    Json root;
    root["schemaVersion"] = snapshot.schemaVersion;
    root["metadata"] = {{"label", snapshot.metadata.label},
                        {"rendererMode", snapshot.metadata.rendererMode},
                        {"sourceCommit", snapshot.metadata.sourceCommit},
                        {"features", snapshot.metadata.features}};
    Json nodes = Json::array();
    for (const RenderGraphDocumentNode* node : SortedById(snapshot.document.nodes))
    {
        nodes.push_back({{"id", IdToHex(node->id)},
                         {"kind", static_cast<int>(node->kind)},
                         {"name", node->name},
                         {"passIndex", node->passIndex},
                         {"firstPass", node->firstPass},
                         {"lastPass", node->lastPass},
                         {"lifetimeKind", static_cast<int>(node->lifetimeKind)},
                         {"resourceKind", static_cast<int>(node->resourceKind)},
                         {"logicalGroupId", IdToHex(node->logicalGroupId)},
                         {"logicalGroupName", node->logicalGroupName},
                         {"physicalIndex", node->physicalIndex},
                         {"pingPongRole", static_cast<int>(node->pingPongRole)}});
    }
    Json pins = Json::array();
    for (const RenderGraphDocumentPin* pin : SortedById(snapshot.document.pins))
    {
        pins.push_back({{"id", IdToHex(pin->id)},
                        {"nodeId", IdToHex(pin->nodeId)},
                        {"direction", static_cast<int>(pin->direction)},
                        {"access", static_cast<int>(pin->access)},
                        {"state", static_cast<uint32_t>(pin->state)}});
    }
    Json links = Json::array();
    for (const RenderGraphDocumentLink* link : SortedById(snapshot.document.links))
    {
        links.push_back({{"id", IdToHex(link->id)},
                         {"fromPinId", IdToHex(link->fromPinId)},
                         {"toPinId", IdToHex(link->toPinId)},
                         {"passNodeId", IdToHex(link->passNodeId)},
                         {"resourceNodeId", IdToHex(link->resourceNodeId)},
                         {"access", static_cast<int>(link->access)},
                         {"state", static_cast<uint32_t>(link->state)}});
    }
    root["document"] = {{"nodes", nodes}, {"pins", pins}, {"links", links}};
    return root.dump(2) + '\n';
}

bool DeserializeRenderGraphSnapshot(const std::string& json,
                                    RenderGraphSnapshot& snapshot,
                                    std::string& error)
{
    try
    {
        const Json root = Json::parse(json);
        if (root.at("schemaVersion").get<uint32_t>() != RenderGraphSnapshot::kSchemaVersion)
        {
            error = "Unsupported RenderGraph snapshot schema version.";
            return false;
        }
        RenderGraphSnapshot result;
        const Json& metadata = root.at("metadata");
        result.metadata.label = metadata.value("label", "");
        result.metadata.rendererMode = metadata.value("rendererMode", "");
        result.metadata.sourceCommit = metadata.value("sourceCommit", "");
        result.metadata.features = metadata.value("features", std::map<std::string, bool>{});
        const Json& document = root.at("document");
        for (const Json& value : document.at("nodes"))
        {
            RenderGraphDocumentNode node;
            if (!HexToId(value.at("id"), node.id))
            {
                throw std::runtime_error("Invalid node ID.");
            }
            node.kind = static_cast<RenderGraphNodeKind>(value.at("kind").get<int>());
            node.name = value.at("name").get<std::string>();
            node.passIndex = value.at("passIndex").get<int>();
            node.firstPass = value.at("firstPass").get<int>();
            node.lastPass = value.at("lastPass").get<int>();
            node.lifetimeKind = static_cast<RenderGraphResourceLifetimeKind>(value.at("lifetimeKind").get<int>());
            node.resourceKind = static_cast<RenderGraphResourceKind>(value.at("resourceKind").get<int>());
            const std::string groupId = value.value("logicalGroupId", "0000000000000000");
            if (groupId != "0000000000000000" && !HexToId(groupId, node.logicalGroupId))
            {
                throw std::runtime_error("Invalid logical group ID.");
            }
            node.logicalGroupName = value.value("logicalGroupName", "");
            node.physicalIndex = value.value("physicalIndex", -1);
            node.pingPongRole = static_cast<RenderGraphPingPongRole>(value.value("pingPongRole", 0));
            result.document.nodes.push_back(std::move(node));
        }
        for (const Json& value : document.at("pins"))
        {
            RenderGraphDocumentPin pin;
            if (!HexToId(value.at("id"), pin.id) || !HexToId(value.at("nodeId"), pin.nodeId))
            {
                throw std::runtime_error("Invalid pin ID.");
            }
            pin.direction = static_cast<RenderGraphPinDirection>(value.at("direction").get<int>());
            pin.access = static_cast<RenderGraphResourceAccess>(value.at("access").get<int>());
            pin.state = static_cast<D3D12_RESOURCE_STATES>(value.at("state").get<uint32_t>());
            result.document.pins.push_back(pin);
        }
        for (const Json& value : document.at("links"))
        {
            RenderGraphDocumentLink link;
            if (!HexToId(value.at("id"), link.id) || !HexToId(value.at("fromPinId"), link.fromPinId) ||
                !HexToId(value.at("toPinId"), link.toPinId) || !HexToId(value.at("passNodeId"), link.passNodeId) ||
                !HexToId(value.at("resourceNodeId"), link.resourceNodeId))
            {
                throw std::runtime_error("Invalid link ID.");
            }
            link.access = static_cast<RenderGraphResourceAccess>(value.at("access").get<int>());
            link.state = static_cast<D3D12_RESOURCE_STATES>(value.at("state").get<uint32_t>());
            result.document.links.push_back(link);
        }

        std::unordered_set<RenderGraphDocumentId> nodeIds;
        std::unordered_set<RenderGraphDocumentId> pinIds;
        for (const RenderGraphDocumentNode& node : result.document.nodes)
        {
            if (!nodeIds.insert(node.id).second)
            {
                throw std::runtime_error("Duplicate node ID.");
            }
        }
        for (const RenderGraphDocumentPin& pin : result.document.pins)
        {
            if (!pinIds.insert(pin.id).second || !nodeIds.contains(pin.nodeId))
            {
                throw std::runtime_error("Duplicate or dangling pin ID.");
            }
        }
        std::unordered_set<RenderGraphDocumentId> linkIds;
        for (const RenderGraphDocumentLink& link : result.document.links)
        {
            if (!linkIds.insert(link.id).second || !pinIds.contains(link.fromPinId) || !pinIds.contains(link.toPinId) ||
                !nodeIds.contains(link.passNodeId) || !nodeIds.contains(link.resourceNodeId))
            {
                throw std::runtime_error("Duplicate or dangling link reference.");
            }
        }
        snapshot = std::move(result);
        error.clear();
        return true;
    }
    catch (const std::exception& exception)
    {
        error = exception.what();
        return false;
    }
}

RenderGraphDocumentDiff DiffRenderGraphDocuments(const RenderGraphDocument& baseline,
                                                 const RenderGraphDocument& current)
{
    RenderGraphDocumentDiff diff;
    std::unordered_map<RenderGraphDocumentId, const RenderGraphDocumentNode*> baselineNodes;
    std::unordered_map<RenderGraphDocumentId, const RenderGraphDocumentNode*> currentNodes;
    for (const RenderGraphDocumentNode& node : baseline.nodes)
    {
        baselineNodes[node.id] = &node;
    }
    for (const RenderGraphDocumentNode& node : current.nodes)
    {
        currentNodes[node.id] = &node;
    }
    for (const auto& [id, node] : currentNodes)
    {
        const auto baselineNode = baselineNodes.find(id);
        if (baselineNode == baselineNodes.end())
        {
            diff.addedNodes.push_back(id);
        }
        else if (!NodesEqual(*baselineNode->second, *node))
        {
            diff.changedNodes.push_back(id);
        }
    }
    for (const auto& [id, node] : baselineNodes)
    {
        if (!currentNodes.contains(id))
        {
            diff.removedNodes.push_back(id);
        }
    }

    std::unordered_map<RenderGraphDocumentId, const RenderGraphDocumentLink*> baselineLinks;
    std::unordered_map<RenderGraphDocumentId, const RenderGraphDocumentLink*> currentLinks;
    for (const RenderGraphDocumentLink& link : baseline.links)
    {
        baselineLinks[link.id] = &link;
    }
    for (const RenderGraphDocumentLink& link : current.links)
    {
        currentLinks[link.id] = &link;
    }
    for (const auto& [id, link] : currentLinks)
    {
        const auto baselineLink = baselineLinks.find(id);
        if (baselineLink == baselineLinks.end())
        {
            diff.addedLinks.push_back(id);
        }
        else if (!LinksEqual(*baselineLink->second, *link))
        {
            diff.changedLinks.push_back(id);
        }
    }
    for (const auto& [id, link] : baselineLinks)
    {
        if (!currentLinks.contains(id))
        {
            diff.removedLinks.push_back(id);
        }
    }

    const auto sortIds = [](auto& ids) {
        std::sort(ids.begin(), ids.end(), [](RenderGraphDocumentId lhs, RenderGraphDocumentId rhs) {
            return lhs.value < rhs.value;
        });
    };
    sortIds(diff.addedNodes);
    sortIds(diff.removedNodes);
    sortIds(diff.changedNodes);
    sortIds(diff.addedLinks);
    sortIds(diff.removedLinks);
    sortIds(diff.changedLinks);
    return diff;
}

} // namespace Engine
