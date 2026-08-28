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
};

using RenderGraphResourceMetadataMap = std::unordered_map<std::string, RenderGraphResourceMetadata>;

struct RenderGraphDocument
{
    std::vector<RenderGraphDocumentNode> nodes;
    std::vector<RenderGraphDocumentPin> pins;
    std::vector<RenderGraphDocumentLink> links;
};

RenderGraphDocument BuildRenderGraphDocument(const std::vector<RenderPass>& renderPasses,
                                             const RenderGraphResourceMetadataMap& resourceMetadata = {});

std::string DumpRenderGraphDocumentText(const RenderGraphDocument& document);
std::string DumpRenderGraphDocumentDot(const RenderGraphDocument& document);
std::string FormatD3D12ResourceStates(D3D12_RESOURCE_STATES states);

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
