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

} // namespace Engine
