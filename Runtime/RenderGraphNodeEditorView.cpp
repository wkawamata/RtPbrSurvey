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

#include <algorithm>
#include <array>
#include <cctype>
#include <deque>
#include <optional>
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

const char* LifetimeKindLabel(Engine::RenderGraphResourceLifetimeKind kind)
{
    switch (kind)
    {
        case Engine::RenderGraphResourceLifetimeKind::Transient:
            return "Transient";
        case Engine::RenderGraphResourceLifetimeKind::Persistent:
            return "Persistent";
        default:
            return "Unknown";
    }
}

const char* PingPongRoleLabel(Engine::RenderGraphPingPongRole role)
{
    switch (role)
    {
        case Engine::RenderGraphPingPongRole::HistoryRead:
            return "History Read";
        case Engine::RenderGraphPingPongRole::CurrentWrite:
            return "Current Write";
        default:
            return "None";
    }
}

const Engine::RenderGraphDocumentNode* FindNode(const Engine::RenderGraphDocument& document,
                                                Engine::RenderGraphDocumentId id)
{
    for (const Engine::RenderGraphDocumentNode& node : document.nodes)
    {
        if (node.id == id)
        {
            return &node;
        }
    }
    return nullptr;
}

bool ContainsCaseInsensitive(const std::string& value, const char* query)
{
    if (query[0] == '\0')
    {
        return true;
    }

    std::string lowerValue = value;
    std::string lowerQuery = query;
    std::transform(lowerValue.begin(),
                   lowerValue.end(),
                   lowerValue.begin(),
                   [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    std::transform(lowerQuery.begin(),
                   lowerQuery.end(),
                   lowerQuery.begin(),
                   [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return lowerValue.find(lowerQuery) != std::string::npos;
}

std::string LogicalResourceName(const std::string& name)
{
    const size_t separator = name.find_last_of('.');
    if (separator == std::string::npos || separator + 1 >= name.size())
    {
        return name;
    }

    const bool numericSuffix = std::all_of(
        name.begin() + separator + 1, name.end(), [](unsigned char character) { return std::isdigit(character) != 0; });
    return numericSuffix ? name.substr(0, separator) : name;
}

std::optional<Engine::RenderGraphDocumentId>
DrawLifetimeTimeline(const Engine::RenderGraphDocument& document,
                     const std::optional<Engine::RenderGraphDocumentId>& selectedNodeId)
{
    std::vector<const Engine::RenderGraphDocumentNode*> resources;
    int lastPass = 0;
    for (const Engine::RenderGraphDocumentNode& node : document.nodes)
    {
        if (node.kind == Engine::RenderGraphNodeKind::Resource)
        {
            resources.push_back(&node);
            lastPass = (std::max)(lastPass, node.lastPass);
        }
    }
    std::sort(resources.begin(),
              resources.end(),
              [](const auto* lhs, const auto* rhs)
              {
                  const std::string lhsLogicalName = LogicalResourceName(lhs->name);
                  const std::string rhsLogicalName = LogicalResourceName(rhs->name);
                  return lhsLogicalName != rhsLogicalName ? lhsLogicalName < rhsLogicalName : lhs->name < rhs->name;
              });

    ImGui::SeparatorText("Resource Lifetime Timeline");
    ImGui::TextDisabled("Pass 0 to %d", lastPass);
    std::optional<Engine::RenderGraphDocumentId> clickedNodeId;
    if (ImGui::BeginTable("RenderGraphLifetimeTable",
                          2,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
                          ImVec2(0.0f, 260.0f)))
    {
        ImGui::TableSetupColumn("Resource", ImGuiTableColumnFlags_WidthFixed, 150.0f);
        ImGui::TableSetupColumn("Lifetime", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        for (const Engine::RenderGraphDocumentNode* resource : resources)
        {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::PushID(reinterpret_cast<void*>(static_cast<uintptr_t>(resource->id.value)));
            const bool selected = selectedNodeId.has_value() && *selectedNodeId == resource->id;
            if (ImGui::Selectable(resource->name.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns))
            {
                clickedNodeId = resource->id;
            }
            ImGui::PopID();

            ImGui::TableSetColumnIndex(1);
            const ImVec2 timelineStart = ImGui::GetCursorScreenPos();
            const float timelineWidth = ImGui::GetContentRegionAvail().x;
            const float rowHeight = ImGui::GetTextLineHeight();
            const float passCount = static_cast<float>((std::max)(lastPass + 1, 1));
            const float start = static_cast<float>((std::max)(resource->firstPass, 0)) / passCount;
            const float end = static_cast<float>((std::max)(resource->lastPass + 1, 1)) / passCount;
            const ImU32 color =
                ImGui::GetColorU32(resource->lifetimeKind == Engine::RenderGraphResourceLifetimeKind::Transient
                                       ? ImVec4(0.95f, 0.45f, 0.12f, 0.90f)
                                       : ImVec4(0.80f, 0.18f, 0.12f, 0.90f));
            ImGui::GetWindowDrawList()->AddRectFilled(
                ImVec2(timelineStart.x + timelineWidth * start, timelineStart.y + 2.0f),
                ImVec2(timelineStart.x + timelineWidth * end, timelineStart.y + rowHeight - 2.0f),
                color,
                3.0f);
            ImGui::Dummy(ImVec2(timelineWidth, rowHeight));
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("%s: [%d, %d] %s",
                                  resource->name.c_str(),
                                  resource->firstPass,
                                  resource->lastPass,
                                  LifetimeKindLabel(resource->lifetimeKind));
            }
        }
        ImGui::EndTable();
    }
    return clickedNodeId;
}

void DrawStateDiagnostics(const Engine::RenderGraphDocument& document,
                          const std::vector<Engine::RenderGraphStateDiagnostic>& diagnostics)
{
    ImGui::SeparatorText("State Diagnostics");
    ImGui::TextDisabled("%zu required barriers", diagnostics.size());
    if (ImGui::BeginTable("RenderGraphStateDiagnosticsTable",
                          3,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
                          ImVec2(0.0f, 220.0f)))
    {
        ImGui::TableSetupColumn("Resource");
        ImGui::TableSetupColumn("Pass");
        ImGui::TableSetupColumn("Transition");
        ImGui::TableHeadersRow();
        for (const Engine::RenderGraphStateDiagnostic& diagnostic : diagnostics)
        {
            const Engine::RenderGraphDocumentNode* resource = FindNode(document, diagnostic.resourceNodeId);
            const Engine::RenderGraphDocumentNode* pass = FindNode(document, diagnostic.afterPassNodeId);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(resource != nullptr ? resource->name.c_str() : "<missing>");
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(pass != nullptr ? pass->name.c_str() : "<missing>");
            ImGui::TableSetColumnIndex(2);
            if (diagnostic.kind == Engine::RenderGraphStateDiagnosticKind::UavBarrierCandidate)
            {
                ImGui::TextUnformatted("UAV barrier");
            }
            else
            {
                const std::string before = Engine::FormatD3D12ResourceStates(diagnostic.beforeState);
                const std::string after = Engine::FormatD3D12ResourceStates(diagnostic.afterState);
                ImGui::TextWrapped("%s -> %s", before.c_str(), after.c_str());
            }
        }
        ImGui::EndTable();
    }
}

struct PassTimingHistory
{
    static constexpr size_t kCapacity = 120;
    std::deque<float> values;

    void Add(float value)
    {
        values.push_back(value);
        if (values.size() > kCapacity)
        {
            values.pop_front();
        }
    }

    float Value(int mode) const
    {
        if (values.empty())
        {
            return 0.0f;
        }
        if (mode == 0)
        {
            return values.back();
        }
        if (mode == 2)
        {
            return *std::max_element(values.begin(), values.end());
        }
        float sum = 0.0f;
        for (const float value : values)
        {
            sum += value;
        }
        return sum / static_cast<float>(values.size());
    }
};

void DrawDetailPanel(const Engine::RenderGraphDocument& document,
                     const std::optional<Engine::RenderGraphDocumentId>& selectedNodeId,
                     const std::unordered_map<int, PassTimingHistory>& passTimings,
                     const PassTimingHistory& totalTiming,
                     int timingMode)
{
    ImGui::TextUnformatted("Node Details");
    ImGui::Separator();

    const Engine::RenderGraphDocumentNode* node =
        selectedNodeId.has_value() ? FindNode(document, *selectedNodeId) : nullptr;
    if (node == nullptr)
    {
        ImGui::TextDisabled("Select a Pass or Resource node.");
        return;
    }

    ImGui::TextWrapped("%s", node->name.c_str());
    if (node->kind == Engine::RenderGraphNodeKind::Pass)
    {
        ImGui::Text("Type: Pass");
        ImGui::Text("Execution order: %d", node->passIndex);
        const auto timing = passTimings.find(node->passIndex);
        if (timing != passTimings.end() && !timing->second.values.empty())
        {
            const float durationMs = timing->second.Value(timingMode);
            const float totalMs = totalTiming.Value(timingMode);
            const float percentage = totalMs > 0.0f ? durationMs * 100.0f / totalMs : 0.0f;
            ImGui::Text("GPU: %.3f ms (%.1f%%)", durationMs, percentage);
        }
        else
        {
            ImGui::TextDisabled("GPU: N/A");
        }
    }
    else
    {
        ImGui::Text("Type: %s", ResourceKindLabel(node->resourceKind));
        ImGui::Text("Lifetime: %s", LifetimeKindLabel(node->lifetimeKind));
        ImGui::Text("Pass range: [%d, %d]", node->firstPass, node->lastPass);
        if (!node->logicalGroupName.empty())
        {
            ImGui::TextWrapped("Logical group: %s", node->logicalGroupName.c_str());
            ImGui::Text("Physical index: %d", node->physicalIndex);
            ImGui::Text("Current role: %s", PingPongRoleLabel(node->pingPongRole));
        }
    }

    ImGui::Spacing();
    ImGui::TextUnformatted(node->kind == Engine::RenderGraphNodeKind::Pass ? "Resources" : "Pass usages");
    if (ImGui::BeginTable("RenderGraphNodeDetailsTable",
                          3,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
    {
        ImGui::TableSetupColumn(node->kind == Engine::RenderGraphNodeKind::Pass ? "Resource" : "Pass");
        ImGui::TableSetupColumn("Access");
        ImGui::TableSetupColumn("State");
        ImGui::TableHeadersRow();

        for (const Engine::RenderGraphDocumentLink& link : document.links)
        {
            const bool matches = node->kind == Engine::RenderGraphNodeKind::Pass ? link.passNodeId == node->id
                                                                                 : link.resourceNodeId == node->id;
            if (!matches)
            {
                continue;
            }

            const Engine::RenderGraphDocumentId relatedId =
                node->kind == Engine::RenderGraphNodeKind::Pass ? link.resourceNodeId : link.passNodeId;
            const Engine::RenderGraphDocumentNode* relatedNode = FindNode(document, relatedId);
            const std::string state = Engine::FormatD3D12ResourceStates(link.state);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(relatedNode != nullptr ? relatedNode->name.c_str() : "<missing>");
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(AccessLabel(link.access));
            ImGui::TableSetColumnIndex(2);
            ImGui::TextWrapped("%s", state.c_str());
        }
        ImGui::EndTable();
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
    std::optional<Engine::RenderGraphDocumentId> selectedNodeId;
    std::array<char, 128> searchText = {};
    bool showPasses = true;
    bool showTextures = true;
    bool showBuffers = true;
    bool showUnknownResources = true;
    bool showTransient = true;
    bool showPersistent = true;
    bool showUnknownLifetime = true;
    bool connectedOnly = false;
    std::unordered_map<int, PassTimingHistory> passTimings;
    PassTimingHistory totalTiming;
    int timingMode = 1;
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

    bool MatchesFilters(const Engine::RenderGraphDocument& document, const Engine::RenderGraphDocumentNode& node) const
    {
        if (!ContainsCaseInsensitive(node.name, searchText.data()))
        {
            return false;
        }

        if (node.kind == Engine::RenderGraphNodeKind::Pass)
        {
            if (!showPasses)
            {
                return false;
            }
        }
        else
        {
            const bool kindMatches =
                (node.resourceKind == Engine::RenderGraphResourceKind::Texture && showTextures) ||
                (node.resourceKind == Engine::RenderGraphResourceKind::Buffer && showBuffers) ||
                (node.resourceKind == Engine::RenderGraphResourceKind::Unknown && showUnknownResources);
            const bool lifetimeMatches =
                (node.lifetimeKind == Engine::RenderGraphResourceLifetimeKind::Transient && showTransient) ||
                (node.lifetimeKind == Engine::RenderGraphResourceLifetimeKind::Persistent && showPersistent) ||
                (node.lifetimeKind == Engine::RenderGraphResourceLifetimeKind::Unknown && showUnknownLifetime);
            if (!kindMatches || !lifetimeMatches)
            {
                return false;
            }
        }

        if (!connectedOnly || !selectedNodeId.has_value() || node.id == *selectedNodeId)
        {
            return true;
        }

        for (const Engine::RenderGraphDocumentLink& link : document.links)
        {
            if ((link.passNodeId == *selectedNodeId && link.resourceNodeId == node.id) ||
                (link.resourceNodeId == *selectedNodeId && link.passNodeId == node.id))
            {
                return true;
            }
        }
        return false;
    }

    void UpdateTimings(const RenderGraphGpuTimingSnapshot* timing)
    {
        if (timing == nullptr || timing->samples.empty())
        {
            return;
        }
        for (const RenderGraphGpuTimingSample& sample : timing->samples)
        {
            passTimings[sample.passIndex].Add(sample.durationMs);
        }
        totalTiming.Add(timing->totalGpuTimeMs);
    }
};

RenderGraphNodeEditorView::RenderGraphNodeEditorView() : m_impl(std::make_unique<Impl>()) {}

RenderGraphNodeEditorView::~RenderGraphNodeEditorView() = default;

void RenderGraphNodeEditorView::Draw(const Engine::RenderGraphDocument& document,
                                     const RenderGraphGpuTimingSnapshot* timing)
{
    m_impl->UpdateTimings(timing);
    const std::vector<Engine::RenderGraphStateDiagnostic> stateDiagnostics =
        Engine::BuildRenderGraphStateDiagnostics(document);
    const bool fitRequested = ImGui::Button("Fit Graph");
    ImGui::SameLine();
    ImGui::TextDisabled("Read-only");
    ImGui::SameLine();
    constexpr const char* timingModes[] = {"GPU Current", "GPU Average (120)", "GPU Max (120)"};
    ImGui::SetNextItemWidth(150.0f);
    ImGui::Combo("##RenderGraphTimingMode", &m_impl->timingMode, timingModes, _countof(timingModes));

    ImGui::SetNextItemWidth(240.0f);
    ImGui::InputTextWithHint(
        "##RenderGraphSearch", "Search Pass or Resource", m_impl->searchText.data(), m_impl->searchText.size());
    ImGui::SameLine();
    const bool focusRequested = ImGui::Button("Focus First Match");
    ImGui::SameLine();
    ImGui::Checkbox("Connected to selection", &m_impl->connectedOnly);

    ImGui::Checkbox("Pass", &m_impl->showPasses);
    ImGui::SameLine();
    ImGui::Checkbox("Texture", &m_impl->showTextures);
    ImGui::SameLine();
    ImGui::Checkbox("Buffer", &m_impl->showBuffers);
    ImGui::SameLine();
    ImGui::Checkbox("Unknown Resource", &m_impl->showUnknownResources);
    ImGui::SameLine();
    ImGui::Checkbox("Transient", &m_impl->showTransient);
    ImGui::SameLine();
    ImGui::Checkbox("Persistent", &m_impl->showPersistent);
    ImGui::SameLine();
    ImGui::Checkbox("Unknown Lifetime", &m_impl->showUnknownLifetime);

    if (m_impl->selectedNodeId.has_value() && FindNode(document, *m_impl->selectedNodeId) == nullptr)
    {
        m_impl->selectedNodeId.reset();
    }

    constexpr float detailWidth = 360.0f;
    const ImVec2 contentSize = ImGui::GetContentRegionAvail();
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float canvasWidth = (std::max)(contentSize.x - detailWidth - spacing, 240.0f);
    ImGui::BeginChild("RenderGraphCanvasPane", ImVec2(canvasWidth, contentSize.y), false);

    NodeEditor::SetCurrentEditor(m_impl->context);
    NodeEditor::Begin("RenderGraphNodeEditor", ImVec2(0.0f, 0.0f));

    size_t resourceIndex = 0;
    bool layoutChanged = false;
    for (const Engine::RenderGraphDocumentNode& node : document.nodes)
    {
        const bool matchesFilters = m_impl->MatchesFilters(document, node);
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
            if (!node.logicalGroupName.empty())
            {
                contentWidth = (std::max)(contentWidth, ImGui::CalcTextSize("[0] Current Write").x);
                contentWidth = (std::max)(contentWidth, ImGui::CalcTextSize(node.logicalGroupName.c_str()).x);
            }
        }
        else
        {
            contentWidth = (std::max)(contentWidth, ImGui::CalcTextSize("GPU 0000.000 ms").x);
        }
        float& stableContentWidth = m_impl->stableContentWidths[node.id.value];
        stableContentWidth = (std::max)(stableContentWidth, contentWidth);
        contentWidth = stableContentWidth;

        ImVec4 backgroundColor = NodeBackgroundColor(node);
        backgroundColor.w *= matchesFilters ? 1.0f : 0.18f;
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, matchesFilters ? 1.0f : 0.28f);
        NodeEditor::PushStyleColor(NodeEditor::StyleColor_NodeBg, backgroundColor);
        NodeEditor::BeginNode(ToNodeId(node.id));
        ImGui::TextUnformatted(node.name.c_str());
        if (node.kind == Engine::RenderGraphNodeKind::Resource)
        {
            ImGui::TextDisabled("%s", ResourceKindLabel(node.resourceKind));
            if (!node.logicalGroupName.empty())
            {
                const ImVec4 roleColor = node.pingPongRole == Engine::RenderGraphPingPongRole::HistoryRead
                                             ? ImVec4(0.30f, 0.85f, 0.90f, 1.0f)
                                             : ImVec4(1.0f, 0.78f, 0.20f, 1.0f);
                ImGui::TextColored(
                    roleColor, "[%d] %s", node.physicalIndex, PingPongRoleLabel(node.pingPongRole));
            }
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
        else
        {
            const auto timingValue = m_impl->passTimings.find(node.passIndex);
            if (timingValue != m_impl->passTimings.end() && !timingValue->second.values.empty())
            {
                ImGui::TextDisabled("GPU %8.3f ms", timingValue->second.Value(m_impl->timingMode));
            }
            else
            {
                ImGui::TextDisabled("GPU N/A");
            }
        }
        NodeEditor::EndNode();
        NodeEditor::PopStyleColor();
        ImGui::PopStyleVar();
    }

    for (const Engine::RenderGraphDocumentLink& link : document.links)
    {
        const Engine::RenderGraphDocumentNode* passNode = FindNode(document, link.passNodeId);
        const Engine::RenderGraphDocumentNode* resourceNode = FindNode(document, link.resourceNodeId);
        const bool matchesFilters = passNode != nullptr && resourceNode != nullptr &&
                                    m_impl->MatchesFilters(document, *passNode) &&
                                    m_impl->MatchesFilters(document, *resourceNode);
        ImVec4 color = link.access == Engine::RenderGraphResourceAccess::Read ? ImVec4(0.35f, 0.70f, 1.0f, 1.0f)
                                                                              : ImVec4(1.0f, 0.65f, 0.25f, 1.0f);
        for (const Engine::RenderGraphStateDiagnostic& diagnostic : stateDiagnostics)
        {
            if (diagnostic.resourceNodeId == link.resourceNodeId && diagnostic.afterPassNodeId == link.passNodeId)
            {
                color = diagnostic.kind == Engine::RenderGraphStateDiagnosticKind::UavBarrierCandidate
                            ? ImVec4(1.0f, 0.90f, 0.20f, 1.0f)
                            : ImVec4(0.95f, 0.30f, 0.95f, 1.0f);
                break;
            }
        }
        color.w = matchesFilters ? 1.0f : 0.10f;
        const NodeEditor::PinId resourcePinId = m_impl->ResourcePinId(link.resourceNodeId, link.access);
        const NodeEditor::PinId fromPinId =
            link.access == Engine::RenderGraphResourceAccess::Read ? resourcePinId : ToPinId(link.fromPinId);
        const NodeEditor::PinId toPinId =
            link.access == Engine::RenderGraphResourceAccess::Read ? ToPinId(link.toPinId) : resourcePinId;
        NodeEditor::Link(ToLinkId(link.id), fromPinId, toPinId, color, 2.0f);
    }

    NodeEditor::End();
    if (focusRequested)
    {
        for (const Engine::RenderGraphDocumentNode& node : document.nodes)
        {
            if (m_impl->MatchesFilters(document, node))
            {
                NodeEditor::ClearSelection();
                NodeEditor::SelectNode(ToNodeId(node.id));
                NodeEditor::NavigateToSelection(true, 0.25f);
                m_impl->selectedNodeId = node.id;
                break;
            }
        }
    }
    if (NodeEditor::HasSelectionChanged())
    {
        NodeEditor::NodeId selectedNode;
        if (NodeEditor::GetSelectedNodes(&selectedNode, 1) == 1)
        {
            m_impl->selectedNodeId.reset();
            for (const Engine::RenderGraphDocumentNode& node : document.nodes)
            {
                if (ToNodeId(node.id) == selectedNode)
                {
                    m_impl->selectedNodeId = node.id;
                    break;
                }
            }
        }
        else
        {
            m_impl->selectedNodeId.reset();
        }
    }
    if (fitRequested || layoutChanged)
    {
        NodeEditor::NavigateToContent(0.0f);
    }
    NodeEditor::SetCurrentEditor(nullptr);
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("RenderGraphDetailPane", ImVec2(0.0f, contentSize.y), true);
    DrawDetailPanel(document, m_impl->selectedNodeId, m_impl->passTimings, m_impl->totalTiming, m_impl->timingMode);
    const std::optional<Engine::RenderGraphDocumentId> timelineSelection =
        DrawLifetimeTimeline(document, m_impl->selectedNodeId);
    DrawStateDiagnostics(document, stateDiagnostics);
    ImGui::EndChild();

    if (timelineSelection.has_value())
    {
        m_impl->selectedNodeId = timelineSelection;
        NodeEditor::SetCurrentEditor(m_impl->context);
        NodeEditor::ClearSelection();
        NodeEditor::SelectNode(ToNodeId(*timelineSelection));
        NodeEditor::NavigateToSelection(false, 0.25f);
        NodeEditor::SetCurrentEditor(nullptr);
    }
}
} // namespace RtPbrSurvey
