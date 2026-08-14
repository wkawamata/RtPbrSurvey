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
} // namespace

struct RenderGraphNodeEditorView::Impl
{
    NodeEditor::EditorContext* context = nullptr;
    std::unordered_set<uint64_t> positionedNodes;

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

    void PositionNode(const Engine::RenderGraphDocumentNode& node, size_t resourceIndex)
    {
        if (!positionedNodes.insert(node.id.value).second)
        {
            return;
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
    }
};

RenderGraphNodeEditorView::RenderGraphNodeEditorView() : m_impl(std::make_unique<Impl>()) {}

RenderGraphNodeEditorView::~RenderGraphNodeEditorView() = default;

void RenderGraphNodeEditorView::Draw(const Engine::RenderGraphDocument& document)
{
    NodeEditor::SetCurrentEditor(m_impl->context);
    NodeEditor::Begin("RenderGraphNodeEditor");

    size_t resourceIndex = 0;
    for (const Engine::RenderGraphDocumentNode& node : document.nodes)
    {
        m_impl->PositionNode(node, resourceIndex);
        if (node.kind == Engine::RenderGraphNodeKind::Resource)
        {
            ++resourceIndex;
        }

        NodeEditor::BeginNode(ToNodeId(node.id));
        ImGui::TextUnformatted(node.name.c_str());
        ImGui::Separator();

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

        if (node.kind == Engine::RenderGraphNodeKind::Resource)
        {
            ImGui::TextDisabled("Lifetime [%d, %d]", node.firstPass, node.lastPass);
        }
        NodeEditor::EndNode();
    }

    for (const Engine::RenderGraphDocumentLink& link : document.links)
    {
        const ImVec4 color = link.access == Engine::RenderGraphResourceAccess::Read ? ImVec4(0.35f, 0.70f, 1.0f, 1.0f)
                                                                                    : ImVec4(1.0f, 0.65f, 0.25f, 1.0f);
        NodeEditor::Link(ToLinkId(link.id), ToPinId(link.fromPinId), ToPinId(link.toPinId), color, 2.0f);
    }

    NodeEditor::End();
    NodeEditor::SetCurrentEditor(nullptr);
}
} // namespace RtPbrSurvey
