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

#include "Runtime/RenderGraphNodeEditorView.h"

#include "third_party/imgui-node-editor/imgui_node_editor.h"

#include <imgui.h>

#include <unordered_map>
#include <unordered_set>

namespace RtPbrSurvey
{
namespace NodeEditor = ax::NodeEditor;

namespace
{
NodeEditor::NodeId ToNodeId(Engine::RenderGraphDocumentId id)
{
    return NodeEditor::NodeId(static_cast<uintptr_t>(id.value));
}

NodeEditor::PinId ToPinId(Engine::RenderGraphDocumentId id)
{
    return NodeEditor::PinId(static_cast<uintptr_t>(id.value));
}

NodeEditor::LinkId ToLinkId(Engine::RenderGraphDocumentId id)
{
    return NodeEditor::LinkId(static_cast<uintptr_t>(id.value));
}

const char* AccessLabel(Engine::RenderGraphResourceAccess access)
{
    return access == Engine::RenderGraphResourceAccess::Read ? "Read" : "Write";
}

const char* ResourceKindLabel(Engine::RenderGraphResourceKind kind)
{
    switch (kind)
    {
        case Engine::RenderGraphResourceKind::Texture:
            return "Texture";
        case Engine::RenderGraphResourceKind::Buffer:
            return "Buffer";
        default:
            return "Resource";
    }
}

ImVec4 NodeBackgroundColor(const Engine::RenderGraphDocumentNode& node)
{
    if (node.kind == Engine::RenderGraphNodeKind::Pass)
    {
        return ImVec4(0.08f, 0.15f, 0.23f, 0.96f);
    }

    switch (node.resourceKind)
    {
        case Engine::RenderGraphResourceKind::Texture:
            return ImVec4(0.30f, 0.16f, 0.04f, 0.96f);
        case Engine::RenderGraphResourceKind::Buffer:
            return ImVec4(0.30f, 0.07f, 0.06f, 0.96f);
        default:
            return ImVec4(0.22f, 0.10f, 0.10f, 0.96f);
    }
}
} // namespace

struct RenderGraphNodeEditorView::Impl
{
    NodeEditor::EditorContext* context = nullptr;
    std::unordered_set<uint64_t> positionedNodes;
    std::unordered_map<uint64_t, float> stableContentWidths;
    struct ResourcePinIds
    {
        NodeEditor::PinId read;
        NodeEditor::PinId write;
    };
    std::unordered_map<uint64_t, ResourcePinIds> resourcePinIds;
    uintptr_t nextSyntheticPinId = UINTPTR_MAX;

    Impl()
    {
        NodeEditor::Config config;
        config.SettingsFile = nullptr;
        context = NodeEditor::CreateEditor(&config);
    }

    ~Impl()
    {
        NodeEditor::DestroyEditor(context);
    }

    bool PositionNode(const Engine::RenderGraphDocumentNode& node, size_t resourceIndex)
    {
        if (!positionedNodes.insert(node.id.value).second)
        {
            return false;
        }

        ImVec2 position;
        if (node.kind == Engine::RenderGraphNodeKind::Pass)
        {
            position = ImVec2(320.0f * static_cast<float>(node.passIndex), 40.0f);
        }
        else
        {
            const float lifetimeCenter = 0.5f * static_cast<float>(node.firstPass + node.lastPass);
            position = ImVec2(320.0f * lifetimeCenter, 280.0f + 105.0f * static_cast<float>(resourceIndex));
        }
        NodeEditor::SetNodePosition(ToNodeId(node.id), position);
        return true;
    }

    NodeEditor::PinId ResourcePinId(Engine::RenderGraphDocumentId nodeId, Engine::RenderGraphResourceAccess access)
    {
        auto [entry, inserted] = resourcePinIds.try_emplace(nodeId.value);
        if (inserted)
        {
            entry->second.read = NodeEditor::PinId(nextSyntheticPinId--);
            entry->second.write = NodeEditor::PinId(nextSyntheticPinId--);
        }
        return access == Engine::RenderGraphResourceAccess::Read ? entry->second.read : entry->second.write;
    }
};

RenderGraphNodeEditorView::RenderGraphNodeEditorView() : m_impl(std::make_unique<Impl>()) {}

RenderGraphNodeEditorView::~RenderGraphNodeEditorView() = default;

void RenderGraphNodeEditorView::Draw(const Engine::RenderGraphDocument& document)
{
    const bool fitRequested = ImGui::Button("Fit Graph");
    ImGui::SameLine();
    ImGui::TextDisabled("Read-only");

    NodeEditor::SetCurrentEditor(m_impl->context);
    const float canvasHeight = (std::max)(ImGui::GetContentRegionAvail().y, 240.0f);
    NodeEditor::Begin("RenderGraphNodeEditor", ImVec2(0.0f, canvasHeight));

    size_t resourceIndex = 0;
    bool layoutChanged = false;
    for (const Engine::RenderGraphDocumentNode& node : document.nodes)
    {
        layoutChanged |= m_impl->PositionNode(node, resourceIndex);
        if (node.kind == Engine::RenderGraphNodeKind::Resource)
        {
            ++resourceIndex;
        }

        float contentWidth = ImGui::CalcTextSize(node.name.c_str()).x;
        size_t readPinCount = 0;
        size_t writePinCount = 0;
        for (const Engine::RenderGraphDocumentPin& pin : document.pins)
        {
            if (pin.nodeId != node.id)
            {
                continue;
            }

            if (pin.access == Engine::RenderGraphResourceAccess::Read)
            {
                ++readPinCount;
            }
            else
            {
                ++writePinCount;
            }

            const bool input = pin.direction == Engine::RenderGraphPinDirection::Input;
            const std::string state = Engine::FormatD3D12ResourceStates(pin.state);
            const std::string label = input ? "-> " + std::string(AccessLabel(pin.access)) + ": " + state
                                            : std::string(AccessLabel(pin.access)) + ": " + state + " ->";
            contentWidth = (std::max)(contentWidth, ImGui::CalcTextSize(label.c_str()).x);
        }
        if (node.kind == Engine::RenderGraphNodeKind::Resource)
        {
            contentWidth = (std::max)(contentWidth, ImGui::CalcTextSize(ResourceKindLabel(node.resourceKind)).x);
            contentWidth = (std::max)(contentWidth, ImGui::CalcTextSize("Read  (000) ->").x);
            contentWidth = (std::max)(contentWidth, ImGui::CalcTextSize("-> Write (000)").x);
            const std::string lifetime =
                "Lifetime [" + std::to_string(node.firstPass) + ", " + std::to_string(node.lastPass) + "]";
            contentWidth = (std::max)(contentWidth, ImGui::CalcTextSize(lifetime.c_str()).x);
        }
        float& stableContentWidth = m_impl->stableContentWidths[node.id.value];
        stableContentWidth = (std::max)(stableContentWidth, contentWidth);
        contentWidth = stableContentWidth;

        NodeEditor::PushStyleColor(NodeEditor::StyleColor_NodeBg, NodeBackgroundColor(node));
        NodeEditor::BeginNode(ToNodeId(node.id));
        ImGui::TextUnformatted(node.name.c_str());
        if (node.kind == Engine::RenderGraphNodeKind::Resource)
        {
            ImGui::TextDisabled("%s", ResourceKindLabel(node.resourceKind));
        }
        const ImVec2 separatorStart = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddLine(separatorStart,
                                            ImVec2(separatorStart.x + contentWidth, separatorStart.y),
                                            ImGui::GetColorU32(ImGuiCol_Separator));
        ImGui::Dummy(ImVec2(contentWidth, 1.0f));

        if (node.kind == Engine::RenderGraphNodeKind::Resource)
        {
            NodeEditor::BeginPin(m_impl->ResourcePinId(node.id, Engine::RenderGraphResourceAccess::Read),
                                 NodeEditor::PinKind::Output);
            ImGui::Text("Read  (%zu) ->", readPinCount);
            NodeEditor::EndPin();
            NodeEditor::BeginPin(m_impl->ResourcePinId(node.id, Engine::RenderGraphResourceAccess::Write),
                                 NodeEditor::PinKind::Input);
            ImGui::Text("-> Write (%zu)", writePinCount);
            NodeEditor::EndPin();
        }
        else
        {
            for (const Engine::RenderGraphDocumentPin& pin : document.pins)
            {
                if (pin.nodeId != node.id)
                {
                    continue;
                }

                const bool input = pin.direction == Engine::RenderGraphPinDirection::Input;
                NodeEditor::BeginPin(ToPinId(pin.id), input ? NodeEditor::PinKind::Input : NodeEditor::PinKind::Output);
                const std::string state = Engine::FormatD3D12ResourceStates(pin.state);
                if (input)
                {
                    ImGui::Text("-> %s: %s", AccessLabel(pin.access), state.c_str());
                }
                else
                {
                    ImGui::Text("%s: %s ->", AccessLabel(pin.access), state.c_str());
                }
                NodeEditor::EndPin();
            }
        }

        if (node.kind == Engine::RenderGraphNodeKind::Resource)
        {
            ImGui::TextDisabled("Lifetime [%d, %d]", node.firstPass, node.lastPass);
        }
        NodeEditor::EndNode();
        NodeEditor::PopStyleColor();
    }

    for (const Engine::RenderGraphDocumentLink& link : document.links)
    {
        const ImVec4 color = link.access == Engine::RenderGraphResourceAccess::Read ? ImVec4(0.35f, 0.70f, 1.0f, 1.0f)
                                                                                    : ImVec4(1.0f, 0.65f, 0.25f, 1.0f);
        const NodeEditor::PinId resourcePinId = m_impl->ResourcePinId(link.resourceNodeId, link.access);
        const NodeEditor::PinId fromPinId =
            link.access == Engine::RenderGraphResourceAccess::Read ? resourcePinId : ToPinId(link.fromPinId);
        const NodeEditor::PinId toPinId =
            link.access == Engine::RenderGraphResourceAccess::Read ? ToPinId(link.toPinId) : resourcePinId;
        NodeEditor::Link(ToLinkId(link.id), fromPinId, toPinId, color, 2.0f);
    }

    NodeEditor::End();
    if (fitRequested || layoutChanged)
    {
        NodeEditor::NavigateToContent(0.0f);
    }
    NodeEditor::SetCurrentEditor(nullptr);
}
} // namespace RtPbrSurvey
