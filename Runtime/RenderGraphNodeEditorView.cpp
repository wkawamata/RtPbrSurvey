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

#include "Runtime/DebugBufferInspector.h"

#include "third_party/imgui-node-editor/imgui_node_editor.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <deque>
#include <fstream>
#include <nlohmann/json.hpp>
#include <optional>
#include <unordered_map>
#include <unordered_set>

namespace RtPbrSurvey
{
namespace NodeEditor = ax::NodeEditor;

namespace
{
std::string NodeEditorSettingsPath()
{
    char appDataPath[MAX_PATH];
    const DWORD appDataLength = GetEnvironmentVariableA("APPDATA", appDataPath, MAX_PATH);
    if (appDataLength == 0 || appDataLength >= MAX_PATH)
    {
        return {};
    }

    std::string settingsDirectory = std::string(appDataPath) + "\\RtPbrSurvey";
    CreateDirectoryA(settingsDirectory.c_str(), nullptr);
    return settingsDirectory + "\\rendergraph_node_editor.json";
}

std::string NodeEditorUeSettingsPath()
{
    std::string path = NodeEditorSettingsPath();
    const size_t extension = path.rfind(".json");
    if (extension != std::string::npos)
    {
        path.insert(extension, "_ue");
    }
    return path;
}

std::string NodeEditorMetadataPath()
{
    std::string path = NodeEditorSettingsPath();
    const size_t fileName = path.find_last_of("\\/");
    if (fileName == std::string::npos)
    {
        return {};
    }
    return path.substr(0, fileName + 1) + "rendergraph_node_metadata.json";
}

using IndexColorMap = std::unordered_map<uint64_t, ImVec4>;

IndexColorMap LoadIndexColors(const std::string& path)
{
    IndexColorMap colors;
    std::ifstream stream(path);
    if (!stream)
    {
        return colors;
    }
    const nlohmann::json settings = nlohmann::json::parse(stream, nullptr, false);
    if (settings.is_discarded() || !settings.contains("indexColors") || !settings["indexColors"].is_object())
    {
        return colors;
    }
    for (const auto& [key, value] : settings["indexColors"].items())
    {
        if (!value.is_array() || value.size() != 4 ||
            !std::all_of(value.begin(), value.end(), [](const auto& component) { return component.is_number(); }))
        {
            continue;
        }
        char* end = nullptr;
        const uint64_t id = std::strtoull(key.c_str(), &end, 10);
        if (end != key.c_str() && *end == '\0')
        {
            colors[id] =
                ImVec4(value[0].get<float>(), value[1].get<float>(), value[2].get<float>(), value[3].get<float>());
        }
    }
    return colors;
}

void SaveIndexColors(const std::string& path, const IndexColorMap& colors)
{
    if (path.empty())
    {
        return;
    }
    nlohmann::json settings;
    nlohmann::json& values = settings["indexColors"];
    for (const auto& [id, color] : colors)
    {
        values[std::to_string(id)] = {color.x, color.y, color.z, color.w};
    }
    std::ofstream stream(path, std::ios::trunc);
    if (stream)
    {
        stream << settings.dump(2);
    }
}

std::unordered_set<uint64_t> LoadSavedNodeIds(const std::string& settingsPath)
{
    std::unordered_set<uint64_t> result;
    if (settingsPath.empty())
    {
        return result;
    }

    std::ifstream stream(settingsPath);
    if (!stream)
    {
        return result;
    }

    const nlohmann::json settings = nlohmann::json::parse(stream, nullptr, false);
    if (settings.is_discarded() || !settings.contains("nodes") || !settings["nodes"].is_object())
    {
        return result;
    }

    for (const auto& [key, value] : settings["nodes"].items())
    {
        constexpr std::string_view prefix = "node:";
        const std::string_view keyView(key);
        const std::string_view idText =
            keyView.compare(0, prefix.size(), prefix) == 0 ? keyView.substr(prefix.size()) : keyView;
        char* end = nullptr;
        const uint64_t id = std::strtoull(idText.data(), &end, 10);
        if (end != idText.data() && *end == '\0')
        {
            result.insert(id);
        }
    }
    return result;
}

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

const Engine::RenderGraphDocumentNode* FindNode(const Engine::RenderGraphDocument& document, NodeEditor::NodeId id)
{
    for (const Engine::RenderGraphDocumentNode& node : document.nodes)
    {
        if (ToNodeId(node.id) == id)
        {
            return &node;
        }
    }
    return nullptr;
}

bool IsDlssSrNode(const Engine::RenderGraphDocumentNode& node)
{
    return node.kind == Engine::RenderGraphNodeKind::Pass && node.name == "TemporalUpscalerPass";
}

bool IsDlssRayReconstructionNode(const Engine::RenderGraphDocumentNode& node)
{
    return node.kind == Engine::RenderGraphNodeKind::Pass && node.name == "DlssRayReconstructionPass";
}

const char* NodeDisplayName(const Engine::RenderGraphDocumentNode& node)
{
    if (IsDlssSrNode(node))
    {
        return "DLSS SR";
    }
    if (IsDlssRayReconstructionNode(node))
    {
        return "DLSS Ray Reconstruction";
    }
    return node.name.c_str();
}

ImVec4 DefaultIndexColor(const Engine::RenderGraphDocumentNode& node)
{
    return IsDlssSrNode(node) || IsDlssRayReconstructionNode(node)
               ? ImVec4(118.0f / 255.0f, 185.0f / 255.0f, 0.0f, 1.0f)
               : ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
}

const char* RelatedResourceName(const Engine::RenderGraphDocument& document, const Engine::RenderGraphDocumentPin& pin)
{
    for (const Engine::RenderGraphDocumentLink& link : document.links)
    {
        if (link.fromPinId == pin.id || link.toPinId == pin.id)
        {
            const Engine::RenderGraphDocumentNode* resource = FindNode(document, link.resourceNodeId);
            return resource != nullptr ? resource->name.c_str() : "<missing>";
        }
    }
    return "<unconnected>";
}

std::string ShortResourceName(const char* name)
{
    constexpr size_t maxLength = 27;
    const std::string value(name);
    return value.size() <= maxLength ? value : value.substr(0, maxLength - 3) + "...";
}

Engine::DebugResourceInspection InspectResource(const Engine::RenderGraphDocumentNode& node,
                                                const Engine::DebugResourceViewRegistry* registry)
{
    if (node.kind != Engine::RenderGraphNodeKind::Resource)
    {
        return {nullptr, "Pass nodes do not select an implicit output resource."};
    }
    if (registry == nullptr)
    {
        return {nullptr, "Resource inspection registry is unavailable."};
    }
    return registry->Inspect(node.name);
}

bool CanOpenPreview(const Engine::DebugResourceInspection& inspection,
                    const RenderGraphResourceActions* resourceActions)
{
    if (!inspection.IsInspectable() || resourceActions == nullptr || !resourceActions->openPreview)
    {
        return false;
    }
    if (inspection.descriptor->viewKind == Engine::DebugResourceViewKind::Texture)
    {
        return true;
    }
    return inspection.descriptor->imageLayout.IsValid(inspection.descriptor->elementCount,
                                                       inspection.descriptor->elementStride) &&
           !inspection.descriptor->sourceDescriptorName.empty();
}

bool DrawResourceActions(const Engine::RenderGraphDocumentNode& node,
                         const Engine::DebugResourceViewRegistry* registry,
                         const RenderGraphResourceActions* resourceActions)
{
    const Engine::DebugResourceInspection inspection = InspectResource(node, registry);
    if (node.resourceKind == Engine::RenderGraphResourceKind::Buffer)
    {
        const bool inspectRequested = ImGui::Button("Inspect Buffer");
        const DebugBufferInspectorModel model = BuildDebugBufferInspectorModel(node, registry);
        const bool canOpenImage = CanOpenPreview(inspection, resourceActions);
        ImGui::SameLine();
        ImGui::BeginDisabled(!canOpenImage);
        if (ImGui::Button("Image Preview"))
        {
            resourceActions->openPreview(*inspection.descriptor, false);
        }
        ImGui::EndDisabled();
        ImGui::TextDisabled("Inspector: %s",
                            model.schemaRegistered ? DebugResourceViewKindLabel(model.viewKind) : "Metadata only");
        ImGui::TextWrapped("%s", model.schemaStatus.c_str());
        return inspectRequested;
    }

    const bool canOpen = CanOpenPreview(inspection, resourceActions);
    const bool previewOpen =
        resourceActions != nullptr && resourceActions->isPreviewOpen && resourceActions->isPreviewOpen(node.name);

    ImGui::BeginDisabled(!canOpen);
    if (ImGui::Button("Preview"))
    {
        resourceActions->openPreview(*inspection.descriptor, false);
    }
    ImGui::SameLine();
    if (ImGui::Button("Pin Preview"))
    {
        resourceActions->openPreview(*inspection.descriptor, true);
    }
    ImGui::EndDisabled();
    if (previewOpen && resourceActions->closePreview)
    {
        ImGui::SameLine();
        if (ImGui::Button("Close Preview"))
        {
            resourceActions->closePreview(node.name);
        }
    }
    if (!inspection.IsInspectable())
    {
        ImGui::TextDisabled("Inspector: %s", inspection.unsupportedReason.c_str());
    }
    else if (resourceActions == nullptr || !resourceActions->openPreview)
    {
        ImGui::TextDisabled("Inspector: Preview manager is unavailable in this host UI.");
    }
    else if (inspection.descriptor->viewKind != Engine::DebugResourceViewKind::Texture)
    {
        ImGui::TextDisabled("Inspector: Compatible Buffer Inspector is not available yet.");
    }
    else
    {
        ImGui::TextDisabled("Inspector: Texture Preview available");
    }
    return false;
}

void DrawResourceThumbnail(const Engine::RenderGraphDocumentNode& node,
                           float contentWidth,
                           bool selected,
                           bool matchesFilters,
                           const Engine::DebugResourceViewRegistry* registry,
                           const RenderGraphResourceActions* resourceActions)
{
    const Engine::DebugResourceInspection inspection = InspectResource(node, registry);
    const bool squareBufferImage = inspection.IsInspectable() &&
                                   inspection.descriptor->viewKind != Engine::DebugResourceViewKind::Texture &&
                                   inspection.descriptor->imageLayout.width == inspection.descriptor->imageLayout.height;
    const ImVec2 thumbnailSize = squareBufferImage ? ImVec2(54.0f, 54.0f) : ImVec2(96.0f, 54.0f);
    const uint64_t textureId = resourceActions != nullptr && resourceActions->thumbnailTextureId
                                   ? resourceActions->thumbnailTextureId(node.name)
                                   : 0;
    const ImVec2 rowStart = ImGui::GetCursorScreenPos();
    ImGui::SetCursorScreenPos(ImVec2(rowStart.x + 0.5f * (contentWidth - thumbnailSize.x), rowStart.y));
    ImGui::PushID(node.name.c_str());
    const ImVec2 imageStart = ImGui::GetCursorScreenPos();
    if (textureId != 0)
    {
        ImGui::Image(ImTextureRef(textureId), thumbnailSize);
    }
    else
    {
        ImGui::InvisibleButton("Thumbnail", thumbnailSize);
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->AddRectFilled(imageStart,
                                ImVec2(imageStart.x + thumbnailSize.x, imageStart.y + thumbnailSize.y),
                                ImGui::GetColorU32(ImVec4(0.08f, 0.08f, 0.08f, 1.0f)));
        const char* placeholder = inspection.IsInspectable() ? "No preview" : "Unavailable";
        const ImVec2 textSize = ImGui::CalcTextSize(placeholder);
        drawList->AddText(ImVec2(imageStart.x + 0.5f * (thumbnailSize.x - textSize.x),
                                 imageStart.y + 0.5f * (thumbnailSize.y - textSize.y)),
                          ImGui::GetColorU32(ImGuiCol_TextDisabled),
                          placeholder);
    }
    ImGui::GetWindowDrawList()->AddRect(imageStart,
                                        ImVec2(imageStart.x + thumbnailSize.x, imageStart.y + thumbnailSize.y),
                                        ImGui::GetColorU32(ImGuiCol_Border));
    const bool thumbnailVisible = ImGui::IsItemVisible();
    if (ImGui::IsItemHovered())
    {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(node.name.c_str());
        if (!inspection.IsInspectable())
        {
            ImGui::TextDisabled("%s", inspection.unsupportedReason.c_str());
        }
        ImGui::EndTooltip();
    }
    const bool previewOpen =
        resourceActions != nullptr && resourceActions->isPreviewOpen && resourceActions->isPreviewOpen(node.name);
    if (CanOpenPreview(inspection, resourceActions) && resourceActions->requestThumbnail && !previewOpen &&
        (selected || (matchesFilters && thumbnailVisible)))
    {
        resourceActions->requestThumbnail(*inspection.descriptor, selected);
    }
    ImGui::PopID();
    ImGui::SetCursorScreenPos(ImVec2(rowStart.x, ImGui::GetCursorScreenPos().y));
}

bool ContainsId(const std::vector<Engine::RenderGraphDocumentId>& ids, Engine::RenderGraphDocumentId id)
{
    return std::find(ids.begin(), ids.end(), id) != ids.end();
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
                          const std::vector<Engine::RenderGraphStateDiagnostic>& diagnostics,
                          const std::vector<Engine::RenderGraphBarrierDiagnostic>* barrierDiagnostics)
{
    ImGui::SeparatorText("State Diagnostics");
    ImGui::TextDisabled("%zu required barriers", diagnostics.size());
    if (barrierDiagnostics != nullptr)
    {
        ImGui::SameLine();
        ImGui::TextDisabled("%zu runtime mismatches", barrierDiagnostics->size());
        for (const Engine::RenderGraphBarrierDiagnostic& diagnostic : *barrierDiagnostics)
        {
            const Engine::RenderGraphDocumentNode* resource = FindNode(document, diagnostic.resourceNodeId);
            const Engine::RenderGraphDocumentNode* pass = FindNode(document, diagnostic.passNodeId);
            const char* status = diagnostic.kind == Engine::RenderGraphBarrierDiagnosticKind::Missing ? "Missing"
                                 : diagnostic.kind == Engine::RenderGraphBarrierDiagnosticKind::Unexpected
                                     ? "Unexpected"
                                     : "State mismatch";
            ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.30f, 1.0f),
                               "%s: %s @ %s",
                               status,
                               resource != nullptr ? resource->name.c_str() : "<missing>",
                               pass != nullptr ? pass->name.c_str() : "<missing>");
            if (diagnostic.kind == Engine::RenderGraphBarrierDiagnosticKind::StateMismatch)
            {
                const std::string expectedBefore = Engine::FormatD3D12ResourceStates(diagnostic.expectedBeforeState);
                const std::string expectedAfter = Engine::FormatD3D12ResourceStates(diagnostic.expectedAfterState);
                const std::string actualBefore = Engine::FormatD3D12ResourceStates(diagnostic.actualBeforeState);
                const std::string actualAfter = Engine::FormatD3D12ResourceStates(diagnostic.actualAfterState);
                ImGui::TextWrapped("  Expected %s -> %s, actual %s -> %s",
                                   expectedBefore.c_str(),
                                   expectedAfter.c_str(),
                                   actualBefore.c_str(),
                                   actualAfter.c_str());
            }
        }
    }
    if (ImGui::BeginTable("RenderGraphStateDiagnosticsTable",
                          4,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
                          ImVec2(0.0f, 220.0f)))
    {
        ImGui::TableSetupColumn("Resource");
        ImGui::TableSetupColumn("Pass");
        ImGui::TableSetupColumn("Transition");
        ImGui::TableSetupColumn("Status");
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
            ImGui::TableSetColumnIndex(3);
            ImGui::TextDisabled("Required");
        }
        if (barrierDiagnostics != nullptr)
        {
            for (const Engine::RenderGraphBarrierDiagnostic& diagnostic : *barrierDiagnostics)
            {
                const Engine::RenderGraphDocumentNode* resource = FindNode(document, diagnostic.resourceNodeId);
                const Engine::RenderGraphDocumentNode* pass = FindNode(document, diagnostic.passNodeId);
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(resource != nullptr ? resource->name.c_str() : "<missing>");
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(pass != nullptr ? pass->name.c_str() : "<missing>");
                ImGui::TableSetColumnIndex(2);
                if (diagnostic.kind == Engine::RenderGraphBarrierDiagnosticKind::StateMismatch)
                {
                    const std::string expectedBefore =
                        Engine::FormatD3D12ResourceStates(diagnostic.expectedBeforeState);
                    const std::string expectedAfter = Engine::FormatD3D12ResourceStates(diagnostic.expectedAfterState);
                    const std::string actualBefore = Engine::FormatD3D12ResourceStates(diagnostic.actualBeforeState);
                    const std::string actualAfter = Engine::FormatD3D12ResourceStates(diagnostic.actualAfterState);
                    ImGui::TextWrapped("Expected %s -> %s, actual %s -> %s",
                                       expectedBefore.c_str(),
                                       expectedAfter.c_str(),
                                       actualBefore.c_str(),
                                       actualAfter.c_str());
                }
                else if (diagnostic.kind == Engine::RenderGraphBarrierDiagnosticKind::Missing)
                {
                    const std::string before = Engine::FormatD3D12ResourceStates(diagnostic.expectedBeforeState);
                    const std::string after = Engine::FormatD3D12ResourceStates(diagnostic.expectedAfterState);
                    ImGui::TextWrapped("%s -> %s", before.c_str(), after.c_str());
                }
                else
                {
                    const std::string before = Engine::FormatD3D12ResourceStates(diagnostic.actualBeforeState);
                    const std::string after = Engine::FormatD3D12ResourceStates(diagnostic.actualAfterState);
                    ImGui::TextWrapped("%s -> %s", before.c_str(), after.c_str());
                }
                ImGui::TableSetColumnIndex(3);
                const char* status = diagnostic.kind == Engine::RenderGraphBarrierDiagnosticKind::Missing ? "Missing"
                                     : diagnostic.kind == Engine::RenderGraphBarrierDiagnosticKind::Unexpected
                                         ? "Unexpected"
                                         : "State mismatch";
                ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.30f, 1.0f), "%s", status);
            }
        }
        ImGui::EndTable();
    }
}

std::optional<Engine::RenderGraphDocumentId>
DrawValidationMessages(const std::vector<Engine::RenderGraphValidationMessage>& messages)
{
    ImGui::SeparatorText("Validation");
    if (messages.empty())
    {
        ImGui::TextDisabled("No validation messages.");
        return std::nullopt;
    }

    std::optional<Engine::RenderGraphDocumentId> focusNodeId;
    if (ImGui::BeginTable("RenderGraphValidationTable",
                          3,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
                          ImVec2(0.0f, 220.0f)))
    {
        ImGui::TableSetupColumn("Severity", ImGuiTableColumnFlags_WidthFixed, 55.0f);
        ImGui::TableSetupColumn("Code", ImGuiTableColumnFlags_WidthFixed, 100.0f);
        ImGui::TableSetupColumn("Message", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();
        for (const Engine::RenderGraphValidationMessage& message : messages)
        {
            const char* severity = message.severity == Engine::RenderGraphValidationSeverity::Error     ? "Error"
                                   : message.severity == Engine::RenderGraphValidationSeverity::Warning ? "Warning"
                                                                                                        : "Info";
            const ImVec4 color =
                message.severity == Engine::RenderGraphValidationSeverity::Error     ? ImVec4(1.0f, 0.30f, 0.25f, 1.0f)
                : message.severity == Engine::RenderGraphValidationSeverity::Warning ? ImVec4(1.0f, 0.75f, 0.20f, 1.0f)
                                                                                     : ImVec4(0.45f, 0.75f, 1.0f, 1.0f);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextColored(color, "%s", severity);
            ImGui::TableSetColumnIndex(1);
            ImGui::PushID(reinterpret_cast<void*>(static_cast<uintptr_t>(message.nodeId.value)));
            ImGui::PushID(reinterpret_cast<void*>(static_cast<uintptr_t>(message.passNodeId.value)));
            if (ImGui::Selectable(message.code.c_str(), false, ImGuiSelectableFlags_SpanAllColumns))
            {
                focusNodeId = message.nodeId;
            }
            ImGui::PopID();
            ImGui::PopID();
            ImGui::TableSetColumnIndex(2);
            ImGui::TextWrapped("%s", message.message.c_str());
        }
        ImGui::EndTable();
    }
    return focusNodeId;
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

struct RelatedResourceInfo
{
    const Engine::RenderGraphDocumentLink* link = nullptr;
    const Engine::RenderGraphDocumentNode* resource = nullptr;
    std::vector<const Engine::RenderGraphDocumentNode*> adjacentPasses;
};

std::vector<RelatedResourceInfo> CollectRelatedResources(
    const Engine::RenderGraphDocument& document,
    Engine::RenderGraphDocumentId passNodeId)
{
    std::vector<RelatedResourceInfo> resources;
    for (const Engine::RenderGraphDocumentLink& link : document.links)
    {
        if (link.passNodeId != passNodeId)
        {
            continue;
        }

        RelatedResourceInfo info;
        info.link = &link;
        info.resource = FindNode(document, link.resourceNodeId);
        if (info.resource == nullptr)
        {
            continue;
        }
        for (const Engine::RenderGraphDocumentLink& adjacentLink : document.links)
        {
            if (adjacentLink.resourceNodeId != link.resourceNodeId || adjacentLink.passNodeId == passNodeId)
            {
                continue;
            }
            const Engine::RenderGraphDocumentNode* adjacentPass = FindNode(document, adjacentLink.passNodeId);
            if (adjacentPass != nullptr &&
                std::find(info.adjacentPasses.begin(), info.adjacentPasses.end(), adjacentPass) ==
                    info.adjacentPasses.end())
            {
                info.adjacentPasses.push_back(adjacentPass);
            }
        }
        resources.push_back(std::move(info));
    }
    return resources;
}

bool IsPreviewOpen(const RenderGraphResourceActions* resourceActions, const std::string& resourceName)
{
    return resourceActions != nullptr && resourceActions->isPreviewOpen &&
        resourceActions->isPreviewOpen(resourceName);
}

bool CanOpenRelatedPreview(const RelatedResourceInfo& info,
                           const Engine::DebugResourceViewRegistry* registry,
                           const RenderGraphResourceActions* resourceActions)
{
    if (info.resource == nullptr)
    {
        return false;
    }
    const Engine::DebugResourceInspection inspection = InspectResource(*info.resource, registry);
    return CanOpenPreview(inspection, resourceActions);
}

void OpenRelatedPreviews(const std::vector<RelatedResourceInfo>& resources,
                         const Engine::DebugResourceViewRegistry* registry,
                         const RenderGraphResourceActions* resourceActions)
{
    if (resourceActions == nullptr || !resourceActions->openPreview)
    {
        return;
    }

    for (const RelatedResourceInfo& info : resources)
    {
        if (info.resource == nullptr || IsPreviewOpen(resourceActions, info.resource->name))
        {
            continue;
        }
        const Engine::DebugResourceInspection inspection = InspectResource(*info.resource, registry);
        if (!CanOpenPreview(inspection, resourceActions))
        {
            continue;
        }
        if (!resourceActions->openPreview(*inspection.descriptor, false))
        {
            break;
        }
    }
}

void DrawRelatedResourceTable(const char* label,
                              Engine::RenderGraphResourceAccess access,
                              const std::vector<RelatedResourceInfo>& resources,
                              const Engine::DebugResourceViewRegistry* registry,
                              const RenderGraphResourceActions* resourceActions)
{
    const size_t resourceCount = static_cast<size_t>(
        std::count_if(resources.begin(), resources.end(), [access](const RelatedResourceInfo& info)
                      { return info.link != nullptr && info.link->access == access; }));
    ImGui::Text("%s (%zu)", label, resourceCount);
    if (resourceCount == 0)
    {
        ImGui::TextDisabled("None");
        return;
    }

    const std::string tableId = std::string("RenderGraphRelated") + label;
    if (!ImGui::BeginTable(tableId.c_str(),
                           5,
                           ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
    {
        return;
    }
    ImGui::TableSetupColumn("Resource", ImGuiTableColumnFlags_WidthStretch, 2.2f);
    ImGui::TableSetupColumn("Kind", ImGuiTableColumnFlags_WidthFixed, 58.0f);
    ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthStretch, 1.4f);
    ImGui::TableSetupColumn(access == Engine::RenderGraphResourceAccess::Read ? "Producer / User" : "Consumer",
                            ImGuiTableColumnFlags_WidthStretch,
                            1.5f);
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 64.0f);
    ImGui::TableHeadersRow();

    for (const RelatedResourceInfo& info : resources)
    {
        if (info.link == nullptr || info.resource == nullptr || info.link->access != access)
        {
            continue;
        }

        ImGui::PushID(info.resource->name.c_str());
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextWrapped("%s", info.resource->name.c_str());
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted(ResourceKindLabel(info.resource->resourceKind));
        ImGui::TableSetColumnIndex(2);
        const std::string state = Engine::FormatD3D12ResourceStates(info.link->state);
        ImGui::TextWrapped("%s", state.c_str());
        ImGui::TableSetColumnIndex(3);
        if (info.adjacentPasses.empty())
        {
            ImGui::TextDisabled("None");
        }
        else
        {
            std::string adjacentPassNames;
            for (const Engine::RenderGraphDocumentNode* adjacentPass : info.adjacentPasses)
            {
                if (!adjacentPassNames.empty())
                {
                    adjacentPassNames += ", ";
                }
                adjacentPassNames += NodeDisplayName(*adjacentPass);
            }
            ImGui::TextWrapped("%s", adjacentPassNames.c_str());
        }
        ImGui::TableSetColumnIndex(4);
        const bool canOpen = CanOpenRelatedPreview(info, registry, resourceActions);
        ImGui::BeginDisabled(!canOpen);
        if (ImGui::SmallButton("Preview"))
        {
            const Engine::DebugResourceInspection inspection = InspectResource(*info.resource, registry);
            resourceActions->openPreview(*inspection.descriptor, false);
        }
        ImGui::EndDisabled();
        ImGui::PopID();
    }
    ImGui::EndTable();
}

std::optional<Engine::RenderGraphDocumentId>
DrawDetailPanel(const Engine::RenderGraphDocument& document,
                const std::optional<Engine::RenderGraphDocumentId>& selectedNodeId,
                const std::unordered_map<int, PassTimingHistory>& passTimings,
                const PassTimingHistory& totalTiming,
                int timingMode,
                const Engine::DebugResourceViewRegistry* resourceViewRegistry,
                const RenderGraphResourceActions* resourceActions,
                const RenderGraphTechnologyMetadata* technologyMetadata,
                bool* isolateRelated,
                bool* fitGraphRequested)
{
    ImGui::TextUnformatted("Node Details");
    ImGui::Separator();

    const Engine::RenderGraphDocumentNode* node =
        selectedNodeId.has_value() ? FindNode(document, *selectedNodeId) : nullptr;
    if (node == nullptr)
    {
        ImGui::TextDisabled("Select a Pass or Resource node.");
        return std::nullopt;
    }

    std::optional<Engine::RenderGraphDocumentId> bufferInspectorRequest;
    const std::vector<RelatedResourceInfo> relatedResources =
        node->kind == Engine::RenderGraphNodeKind::Pass ? CollectRelatedResources(document, node->id)
                                                       : std::vector<RelatedResourceInfo>{};

    ImGui::TextWrapped("%s", NodeDisplayName(*node));
    if (node->kind == Engine::RenderGraphNodeKind::Pass)
    {
        ImGui::Text("Type: Pass");
        if (IsDlssSrNode(*node) || IsDlssRayReconstructionNode(*node))
        {
            ImGui::Text("Stable identity: %s", node->name.c_str());
            ImGui::TextUnformatted("Technology: NVIDIA DLSS");
            const std::string* version = nullptr;
            if (technologyMetadata != nullptr)
            {
                version = IsDlssSrNode(*node) ? &technologyMetadata->dlssSrVersionText
                                              : &technologyMetadata->dlssRayReconstructionVersionText;
            }
            ImGui::TextWrapped("Version: %s",
                               version != nullptr && !version->empty() ? version->c_str() : "Version unavailable");
        }
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

        if (isolateRelated != nullptr)
        {
            if (ImGui::Checkbox("Isolate Related", isolateRelated) && fitGraphRequested != nullptr)
            {
                *fitGraphRequested = true;
            }
        }
        ImGui::SameLine();
        const bool canOpenAll = resourceActions != nullptr && resourceActions->openPreview &&
            std::any_of(relatedResources.begin(),
                        relatedResources.end(),
                        [resourceViewRegistry, resourceActions](const RelatedResourceInfo& info)
                        {
                            return info.resource != nullptr &&
                                !IsPreviewOpen(resourceActions, info.resource->name) &&
                                CanOpenRelatedPreview(info, resourceViewRegistry, resourceActions);
                        });
        ImGui::BeginDisabled(!canOpenAll);
        if (ImGui::Button("Preview All"))
        {
            OpenRelatedPreviews(relatedResources, resourceViewRegistry, resourceActions);
        }
        ImGui::EndDisabled();

        ImGui::Spacing();
        DrawRelatedResourceTable("Inputs",
                                 Engine::RenderGraphResourceAccess::Read,
                                 relatedResources,
                                 resourceViewRegistry,
                                 resourceActions);
        ImGui::Spacing();
        DrawRelatedResourceTable("Outputs",
                                 Engine::RenderGraphResourceAccess::Write,
                                 relatedResources,
                                 resourceViewRegistry,
                                 resourceActions);
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
        if (DrawResourceActions(*node, resourceViewRegistry, resourceActions))
        {
            bufferInspectorRequest = node->id;
        }
    }

    if (node->kind != Engine::RenderGraphNodeKind::Pass)
    {
        ImGui::Spacing();
        ImGui::TextUnformatted("Pass usages");
        if (ImGui::BeginTable("RenderGraphNodeDetailsTable",
                              3,
                              ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
        {
            ImGui::TableSetupColumn("Pass");
            ImGui::TableSetupColumn("Access");
            ImGui::TableSetupColumn("State");
            ImGui::TableHeadersRow();

            for (const Engine::RenderGraphDocumentLink& link : document.links)
            {
                if (link.resourceNodeId != node->id)
                {
                    continue;
                }

                const Engine::RenderGraphDocumentNode* relatedNode = FindNode(document, link.passNodeId);
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
    return bufferInspectorRequest;
}

void DrawBufferInspectorWindow(const Engine::RenderGraphDocument& document,
                               Engine::RenderGraphDocumentId nodeId,
                               const Engine::DebugResourceViewRegistry* registry,
                               const RenderGraphResourceActions* resourceActions,
                               bool requestFocus,
                               bool& open)
{
    const Engine::RenderGraphDocumentNode* node = FindNode(document, nodeId);
    if (node == nullptr || node->resourceKind != Engine::RenderGraphResourceKind::Buffer)
    {
        open = false;
        return;
    }

    if (requestFocus)
    {
        ImGui::SetNextWindowFocus();
    }
    ImGui::SetNextWindowSize(ImVec2(460.0f, 420.0f), ImGuiCond_FirstUseEver);
    const std::string title = "Buffer Inspector: " + node->name + "###RenderGraphBufferInspector";
    if (!ImGui::Begin(title.c_str(), &open))
    {
        ImGui::End();
        return;
    }

    const DebugBufferInspectorModel model = BuildDebugBufferInspectorModel(*node, registry);
    ImGui::TextWrapped("%s", model.resourceName.c_str());
    ImGui::Separator();
    ImGui::Text("Lifetime: %s", LifetimeKindLabel(node->lifetimeKind));
    ImGui::Text("Pass range: [%d, %d]", node->firstPass, node->lastPass);
    ImGui::Text("Schema: %s", model.schemaRegistered ? DebugResourceViewKindLabel(model.viewKind) : "Unregistered");
    ImGui::Text("DXGI format: %u", static_cast<unsigned int>(model.format));
    ImGui::Text("Elements: %u", model.elementCount);
    ImGui::Text("Stride: %u bytes", model.elementStride);
    ImGui::Text("Estimated size: %llu bytes", static_cast<unsigned long long>(model.estimatedByteSize));
    ImGui::TextWrapped("%s", model.schemaStatus.c_str());

    ImGui::Spacing();
    ImGui::TextUnformatted("Visualization");
    ImGui::Separator();
    ImGui::TextDisabled("Metadata");
    if (model.imageLayoutRegistered)
    {
        const uint32_t rowStride =
            model.imageLayout.rowStrideElements != 0 ? model.imageLayout.rowStrideElements : model.imageLayout.width;
        ImGui::Text("Image layout: %u x %u", model.imageLayout.width, model.imageLayout.height);
        ImGui::Text("Row stride: %u elements", rowStride);
        ImGui::Text("Component: %s x%u at byte %u",
                    DebugBufferComponentTypeLabel(model.imageLayout.componentType),
                    model.imageLayout.componentCount,
                    model.imageLayout.componentOffsetBytes);
        const Engine::DebugResourceInspection inspection = InspectResource(*node, registry);
        const bool canOpenImage = CanOpenPreview(inspection, resourceActions);
        ImGui::BeginDisabled(!canOpenImage);
        if (ImGui::Button("Image Preview"))
        {
            resourceActions->openPreview(*inspection.descriptor, false);
        }
        ImGui::EndDisabled();
        if (!canOpenImage)
        {
            ImGui::TextDisabled("Image Preview is unavailable for this Buffer.");
        }
    }
    else
    {
        ImGui::TextDisabled("Image/Heatmap requires a registered 2D element layout.");
    }

    ImGui::Spacing();
    ImGui::TextUnformatted("Pass usages");
    if (ImGui::BeginTable("BufferInspectorUsages",
                          3,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
    {
        ImGui::TableSetupColumn("Pass");
        ImGui::TableSetupColumn("Access");
        ImGui::TableSetupColumn("State");
        ImGui::TableHeadersRow();
        for (const Engine::RenderGraphDocumentLink& link : document.links)
        {
            if (link.resourceNodeId != node->id)
            {
                continue;
            }
            const Engine::RenderGraphDocumentNode* pass = FindNode(document, link.passNodeId);
            const std::string state = Engine::FormatD3D12ResourceStates(link.state);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(pass != nullptr ? pass->name.c_str() : "<missing>");
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(AccessLabel(link.access));
            ImGui::TableSetColumnIndex(2);
            ImGui::TextWrapped("%s", state.c_str());
        }
        ImGui::EndTable();
    }
    ImGui::End();
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
    std::array<std::string, 2> settingsPaths;
    std::array<NodeEditor::EditorContext*, 2> contexts = {};
    std::array<std::unordered_set<uint64_t>, 2> positionedNodes;
    std::array<std::unordered_set<uint64_t>, 2> savedNodeIds;
    std::array<std::unordered_map<uint64_t, float>, 2> stableContentWidths;
    std::string metadataPath;
    IndexColorMap indexColors;
    int layoutMode = 0;
    struct ResourcePinIds
    {
        NodeEditor::PinId read;
        NodeEditor::PinId write;
    };
    std::unordered_map<uint64_t, ResourcePinIds> resourcePinIds;
    std::optional<Engine::RenderGraphDocumentId> selectedNodeId;
    std::optional<Engine::RenderGraphDocumentId> bufferInspectorNodeId;
    bool bufferInspectorOpen = false;
    bool bufferInspectorFocusRequested = false;
    std::array<char, 128> searchText = {};
    bool showPasses = true;
    bool showTextures = true;
    bool showBuffers = true;
    bool showUnknownResources = true;
    bool showTransient = true;
    bool showPersistent = true;
    bool showUnknownLifetime = true;
    bool connectedOnly = false;
    bool fitGraphRequested = false;
    std::unordered_map<int, PassTimingHistory> passTimings;
    PassTimingHistory totalTiming;
    int timingMode = 1;
    std::optional<Engine::RenderGraphDocument> baselineDocument;
    uintptr_t nextSyntheticPinId = UINTPTR_MAX;

    Impl()
        : settingsPaths{NodeEditorSettingsPath(), NodeEditorUeSettingsPath()}, metadataPath(NodeEditorMetadataPath()),
          indexColors(LoadIndexColors(metadataPath))
    {
        for (size_t i = 0; i < contexts.size(); ++i)
        {
            savedNodeIds[i] = LoadSavedNodeIds(settingsPaths[i]);
            NodeEditor::Config config;
            config.SettingsFile = settingsPaths[i].empty() ? nullptr : settingsPaths[i].c_str();
            config.EnableSmoothZoom = true;
            config.SmoothZoomPower = 1.05f;
            contexts[i] = NodeEditor::CreateEditor(&config);
        }
    }

    ~Impl()
    {
        for (NodeEditor::EditorContext* context : contexts)
        {
            NodeEditor::DestroyEditor(context);
        }
    }

    NodeEditor::EditorContext* Context() const
    {
        return contexts[layoutMode];
    }

    ImVec4 IndexColor(const Engine::RenderGraphDocumentNode& node) const
    {
        const auto color = indexColors.find(node.id.value);
        return color != indexColors.end() ? color->second : DefaultIndexColor(node);
    }

    bool PositionNode(const Engine::RenderGraphDocument& document,
                      const Engine::RenderGraphDocumentNode& node,
                      size_t resourceIndex)
    {
        if (!positionedNodes[layoutMode].insert(node.id.value).second)
        {
            return false;
        }
        if (savedNodeIds[layoutMode].contains(node.id.value))
        {
            return false;
        }

        ImVec2 position;
        if (node.kind == Engine::RenderGraphNodeKind::Pass)
        {
            const float spacing = layoutMode == 0 ? 320.0f : 430.0f;
            position = ImVec2(spacing * static_cast<float>(node.passIndex), 40.0f);
            const float rowSpacing = layoutMode == 0 ? 240.0f : 340.0f;
            bool overlapsExistingPass = false;
            do
            {
                overlapsExistingPass = false;
                for (const Engine::RenderGraphDocumentNode& existingNode : document.nodes)
                {
                    if (existingNode.id == node.id || existingNode.kind != Engine::RenderGraphNodeKind::Pass ||
                        (!positionedNodes[layoutMode].contains(existingNode.id.value) &&
                         !savedNodeIds[layoutMode].contains(existingNode.id.value)))
                    {
                        continue;
                    }

                    const ImVec2 existingPosition = NodeEditor::GetNodePosition(ToNodeId(existingNode.id));
                    const bool overlapsX = position.x > existingPosition.x - spacing * 0.75f &&
                        position.x < existingPosition.x + spacing * 0.75f;
                    const bool overlapsY = position.y > existingPosition.y - rowSpacing * 0.75f &&
                        position.y < existingPosition.y + rowSpacing * 0.75f;
                    if (overlapsX && overlapsY)
                    {
                        overlapsExistingPass = true;
                        position.y += rowSpacing;
                        break;
                    }
                }
            } while (overlapsExistingPass);
        }
        else
        {
            const float lifetimeCenter = 0.5f * static_cast<float>(node.firstPass + node.lastPass);
            const float spacing = layoutMode == 0 ? 320.0f : 430.0f;
            const float rowSpacing = layoutMode == 0 ? 105.0f : 130.0f;
            position = ImVec2(spacing * lifetimeCenter, 280.0f + rowSpacing * static_cast<float>(resourceIndex));
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

        const Engine::RenderGraphDocumentNode* selectedNode = FindNode(document, *selectedNodeId);
        if (selectedNode == nullptr)
        {
            return true;
        }

        if (selectedNode->kind == Engine::RenderGraphNodeKind::Pass)
        {
            std::vector<Engine::RenderGraphDocumentId> relatedResourceIds;
            for (const Engine::RenderGraphDocumentLink& link : document.links)
            {
                if (link.passNodeId == selectedNode->id)
                {
                    relatedResourceIds.push_back(link.resourceNodeId);
                }
            }
            if (node.kind == Engine::RenderGraphNodeKind::Resource &&
                std::find(relatedResourceIds.begin(), relatedResourceIds.end(), node.id) != relatedResourceIds.end())
            {
                return true;
            }
            if (node.kind == Engine::RenderGraphNodeKind::Pass)
            {
                return std::any_of(document.links.begin(),
                                   document.links.end(),
                                   [&node, &relatedResourceIds](const Engine::RenderGraphDocumentLink& link)
                                   {
                                       return link.passNodeId == node.id &&
                                           std::find(relatedResourceIds.begin(), relatedResourceIds.end(),
                                                     link.resourceNodeId) != relatedResourceIds.end();
                                   });
            }
            return false;
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
                                     const RenderGraphGpuTimingSnapshot* timing,
                                     const std::vector<Engine::RenderGraphBarrierDiagnostic>* barrierDiagnostics,
                                     const Engine::DebugResourceViewRegistry* resourceViewRegistry,
                                     const RenderGraphResourceActions* resourceActions,
                                     const RenderGraphTechnologyMetadata* technologyMetadata)
{
    m_impl->UpdateTimings(timing);
    const std::vector<Engine::RenderGraphStateDiagnostic> stateDiagnostics =
        Engine::BuildRenderGraphStateDiagnostics(document);
    const std::vector<Engine::RenderGraphValidationMessage> validationMessages =
        Engine::ValidateRenderGraphDocument(document);
    const bool deferredFitRequested = m_impl->fitGraphRequested;
    m_impl->fitGraphRequested = false;
    const int previousLayoutMode = m_impl->layoutMode;
    ImGui::RadioButton("Compact", &m_impl->layoutMode, 0);
    ImGui::SameLine();
    ImGui::RadioButton("UE-style", &m_impl->layoutMode, 1);
    ImGui::SameLine();
    const bool fitRequested = ImGui::Button("Fit Graph");
    ImGui::SameLine();
    ImGui::BeginDisabled(!m_impl->selectedNodeId.has_value());
    const bool focusSelectedRequested = ImGui::Button("Focus Selected");
    ImGui::EndDisabled();
    ImGui::SameLine();
    const bool resetLayoutRequested = ImGui::Button("Reset Saved Layout");
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("Discard saved node positions and restore the automatic layout.");
    }
    if (resetLayoutRequested)
    {
        m_impl->positionedNodes[m_impl->layoutMode].clear();
        m_impl->savedNodeIds[m_impl->layoutMode].clear();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("Read-only");
    ImGui::SameLine();
    constexpr const char* timingModes[] = {"GPU Current", "GPU Average (120)", "GPU Max (120)"};
    ImGui::SetNextItemWidth(150.0f);
    ImGui::Combo("##RenderGraphTimingMode", &m_impl->timingMode, timingModes, _countof(timingModes));
    ImGui::SameLine();
    if (ImGui::Button("Set Baseline"))
    {
        m_impl->baselineDocument = document;
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!m_impl->baselineDocument.has_value());
    if (ImGui::Button("Clear Baseline"))
    {
        m_impl->baselineDocument.reset();
    }
    ImGui::EndDisabled();

    const std::optional<Engine::RenderGraphDocumentDiff> snapshotDiff =
        m_impl->baselineDocument.has_value()
            ? std::optional<Engine::RenderGraphDocumentDiff>(
                  Engine::DiffRenderGraphDocuments(*m_impl->baselineDocument, document))
            : std::nullopt;
    if (snapshotDiff.has_value())
    {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.75f, 0.75f, 0.75f, 1.0f),
                           "Nodes +%zu -%zu ~%zu  Links +%zu -%zu ~%zu",
                           snapshotDiff->addedNodes.size(),
                           snapshotDiff->removedNodes.size(),
                           snapshotDiff->changedNodes.size(),
                           snapshotDiff->addedLinks.size(),
                           snapshotDiff->removedLinks.size(),
                           snapshotDiff->changedLinks.size());
    }

    ImGui::SetNextItemWidth(240.0f);
    ImGui::InputTextWithHint(
        "##RenderGraphSearch", "Search Pass or Resource", m_impl->searchText.data(), m_impl->searchText.size());
    ImGui::SameLine();
    const bool focusRequested = ImGui::Button("Focus First Match");
    ImGui::SameLine();
    ImGui::BeginDisabled(!m_impl->selectedNodeId.has_value());
    const bool isolateRelatedChanged = ImGui::Checkbox("Isolate Related", &m_impl->connectedOnly);
    ImGui::EndDisabled();

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
    if (resourceActions != nullptr)
    {
        ImGui::SameLine();
        ImGui::TextDisabled("Previews: %zu / %zu (Pinned: %zu)",
                            resourceActions->activePreviewCount,
                            resourceActions->maxPreviewCount,
                            resourceActions->pinnedPreviewCount);
        ImGui::SameLine();
        ImGui::BeginDisabled(resourceActions->activePreviewCount == 0 || !resourceActions->closeAllPreviews);
        if (ImGui::SmallButton("Close All Previews"))
        {
            resourceActions->closeAllPreviews();
        }
        ImGui::EndDisabled();
    }
    if (m_impl->selectedNodeId.has_value())
    {
        const Engine::RenderGraphDocumentNode* selectedNode = FindNode(document, *m_impl->selectedNodeId);
        if (selectedNode != nullptr && selectedNode->kind == Engine::RenderGraphNodeKind::Resource)
        {
            ImGui::SameLine();
            ImGui::TextDisabled("Selected: %s", selectedNode->name.c_str());
        }
    }

    if (m_impl->selectedNodeId.has_value() && FindNode(document, *m_impl->selectedNodeId) == nullptr)
    {
        m_impl->selectedNodeId.reset();
    }

    constexpr float detailWidth = 360.0f;
    const ImVec2 contentSize = ImGui::GetContentRegionAvail();
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float canvasWidth = (std::max)(contentSize.x - detailWidth - spacing, 240.0f);
    ImGui::BeginChild("RenderGraphCanvasPane", ImVec2(canvasWidth, contentSize.y), false);

    NodeEditor::SetCurrentEditor(m_impl->Context());
    NodeEditor::Begin("RenderGraphNodeEditor", ImVec2(0.0f, 0.0f));

    std::vector<const Engine::RenderGraphDocumentNode*> layoutResources;
    for (const Engine::RenderGraphDocumentNode& node : document.nodes)
    {
        if (node.kind == Engine::RenderGraphNodeKind::Resource)
        {
            layoutResources.push_back(&node);
        }
    }
    const auto resourceKindRank = [](Engine::RenderGraphResourceKind kind)
    {
        switch (kind)
        {
            case Engine::RenderGraphResourceKind::Texture:
                return 0;
            case Engine::RenderGraphResourceKind::Buffer:
                return 1;
            default:
                return 2;
        }
    };
    std::sort(layoutResources.begin(),
              layoutResources.end(),
              [&resourceKindRank](const auto* lhs, const auto* rhs)
              {
                  const int lhsKind = resourceKindRank(lhs->resourceKind);
                  const int rhsKind = resourceKindRank(rhs->resourceKind);
                  if (lhsKind != rhsKind)
                  {
                      return lhsKind < rhsKind;
                  }
                  const std::string lhsLogicalName = LogicalResourceName(lhs->name);
                  const std::string rhsLogicalName = LogicalResourceName(rhs->name);
                  if (lhsLogicalName != rhsLogicalName)
                  {
                      return lhsLogicalName < rhsLogicalName;
                  }
                  if (lhs->physicalIndex != rhs->physicalIndex)
                  {
                      return lhs->physicalIndex < rhs->physicalIndex;
                  }
                  return lhs->name < rhs->name;
              });
    std::unordered_map<Engine::RenderGraphDocumentId, size_t> layoutRows;
    size_t layoutRow = 0;
    int previousKind = -1;
    for (const Engine::RenderGraphDocumentNode* resource : layoutResources)
    {
        const int kind = resourceKindRank(resource->resourceKind);
        if (previousKind >= 0 && kind != previousKind)
        {
            layoutRow += 1;
        }
        layoutRows[resource->id] = layoutRow++;
        previousKind = kind;
    }

    bool layoutChanged = false;
    for (const Engine::RenderGraphDocumentNode& node : document.nodes)
    {
        const bool matchesFilters = m_impl->MatchesFilters(document, node);
        if (m_impl->connectedOnly && !matchesFilters)
        {
            continue;
        }
        const size_t resourceRow = node.kind == Engine::RenderGraphNodeKind::Resource ? layoutRows.at(node.id) : 0;
        layoutChanged |= m_impl->PositionNode(document, node, resourceRow);

        constexpr float uePinColumnGap = 16.0f;
        constexpr float ueThumbnailWidth = 96.0f;
        const float titleLeadingWidth = 10.0f + ImGui::GetStyle().ItemSpacing.x;
        float contentWidth = titleLeadingWidth + ImGui::CalcTextSize(NodeDisplayName(node)).x;
        size_t readPinCount = 0;
        size_t writePinCount = 0;
        std::vector<const Engine::RenderGraphDocumentPin*> inputPins;
        std::vector<const Engine::RenderGraphDocumentPin*> outputPins;
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

            if (m_impl->layoutMode == 0)
            {
                const bool input = pin.direction == Engine::RenderGraphPinDirection::Input;
                const std::string state = Engine::FormatD3D12ResourceStates(pin.state);
                const std::string label = input ? "-> " + std::string(AccessLabel(pin.access)) + ": " + state
                                                : std::string(AccessLabel(pin.access)) + ": " + state + " ->";
                contentWidth = (std::max)(contentWidth, ImGui::CalcTextSize(label.c_str()).x);
            }
            else if (node.kind == Engine::RenderGraphNodeKind::Pass)
            {
                (pin.direction == Engine::RenderGraphPinDirection::Input ? inputPins : outputPins).push_back(&pin);
            }
        }
        if (m_impl->layoutMode == 0 && node.kind == Engine::RenderGraphNodeKind::Resource)
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
        else if (m_impl->layoutMode == 0)
        {
            contentWidth = (std::max)(contentWidth, ImGui::CalcTextSize("GPU 0000.000 ms").x);
        }
        if (m_impl->layoutMode == 1)
        {
            if (node.kind == Engine::RenderGraphNodeKind::Resource)
            {
                const std::string writeLabel = "-> Write (" + std::to_string(writePinCount) + ")";
                const std::string readLabel = "Read (" + std::to_string(readPinCount) + ") ->";
                contentWidth = (std::max)(contentWidth,
                                          ImGui::CalcTextSize(writeLabel.c_str()).x + uePinColumnGap +
                                              ImGui::CalcTextSize(readLabel.c_str()).x);
                contentWidth = (std::max)(contentWidth, ImGui::CalcTextSize(ResourceKindLabel(node.resourceKind)).x);
                const std::string lifetime =
                    "Lifetime [" + std::to_string(node.firstPass) + ", " + std::to_string(node.lastPass) + "]";
                contentWidth = (std::max)(contentWidth, ImGui::CalcTextSize(lifetime.c_str()).x);
                if (!node.logicalGroupName.empty())
                {
                    const std::string role = "[" + std::to_string(node.physicalIndex) + "] " +
                        PingPongRoleLabel(node.pingPongRole);
                    contentWidth = (std::max)(contentWidth, ImGui::CalcTextSize(role.c_str()).x);
                }
                contentWidth = (std::max)(contentWidth, ueThumbnailWidth);
            }
            else
            {
                const auto stablePinOrder = [&document](const auto* lhs, const auto* rhs)
                {
                    const std::string_view lhsName = RelatedResourceName(document, *lhs);
                    const std::string_view rhsName = RelatedResourceName(document, *rhs);
                    return lhsName != rhsName ? lhsName < rhsName : lhs->id.value < rhs->id.value;
                };
                std::sort(inputPins.begin(), inputPins.end(), stablePinOrder);
                std::sort(outputPins.begin(), outputPins.end(), stablePinOrder);
                const size_t rowCount = (std::max)(inputPins.size(), outputPins.size());
                for (size_t row = 0; row < rowCount; ++row)
                {
                    const std::string inputLabel = row < inputPins.size()
                        ? "-> " + ShortResourceName(RelatedResourceName(document, *inputPins[row]))
                        : std::string{};
                    const std::string outputLabel = row < outputPins.size()
                        ? ShortResourceName(RelatedResourceName(document, *outputPins[row])) + " ->"
                        : std::string{};
                    const float columnGap = !inputLabel.empty() && !outputLabel.empty() ? uePinColumnGap : 0.0f;
                    const float rowWidth = ImGui::CalcTextSize(inputLabel.c_str()).x + columnGap +
                        ImGui::CalcTextSize(outputLabel.c_str()).x;
                    contentWidth = (std::max)(contentWidth, rowWidth);
                }
                contentWidth = (std::max)(contentWidth, ImGui::CalcTextSize("GPU 0000.000 ms").x);
            }
        }
        else
        {
            float& stableContentWidth = m_impl->stableContentWidths[m_impl->layoutMode][node.id.value];
            stableContentWidth = (std::max)(stableContentWidth, contentWidth);
            contentWidth = stableContentWidth;
        }

        ImVec4 backgroundColor = NodeBackgroundColor(node);
        if (snapshotDiff.has_value() && ContainsId(snapshotDiff->addedNodes, node.id))
        {
            backgroundColor = ImVec4(0.08f, 0.32f, 0.14f, 0.96f);
        }
        else if (snapshotDiff.has_value() && ContainsId(snapshotDiff->changedNodes, node.id))
        {
            backgroundColor = ImVec4(0.38f, 0.25f, 0.05f, 0.96f);
        }
        backgroundColor.w *= matchesFilters ? 1.0f : 0.18f;
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, matchesFilters ? 1.0f : 0.28f);
        NodeEditor::PushStyleColor(NodeEditor::StyleColor_NodeBg, backgroundColor);
        if (m_impl->layoutMode == 1)
        {
            NodeEditor::PushStyleVar(NodeEditor::StyleVar_NodePadding, ImVec4(4.0f, 4.0f, 4.0f, 4.0f));
        }
        NodeEditor::BeginNode(ToNodeId(node.id));
        const ImVec2 indexColorStart = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(10.0f, 10.0f));
        ImGui::GetWindowDrawList()->AddRectFilled(indexColorStart,
                                                  ImVec2(indexColorStart.x + 10.0f, indexColorStart.y + 10.0f),
                                                  ImGui::GetColorU32(m_impl->IndexColor(node)));
        ImGui::SameLine();
        if (m_impl->layoutMode == 1)
        {
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + contentWidth - 18.0f);
            ImGui::TextWrapped("%s", NodeDisplayName(node));
            ImGui::PopTextWrapPos();
        }
        else
        {
            ImGui::TextUnformatted(NodeDisplayName(node));
        }
        if (node.kind == Engine::RenderGraphNodeKind::Resource)
        {
            ImGui::TextDisabled("%s", ResourceKindLabel(node.resourceKind));
            if (!node.logicalGroupName.empty())
            {
                const ImVec4 roleColor = node.pingPongRole == Engine::RenderGraphPingPongRole::HistoryRead
                                             ? ImVec4(0.30f, 0.85f, 0.90f, 1.0f)
                                             : ImVec4(1.0f, 0.78f, 0.20f, 1.0f);
                ImGui::TextColored(roleColor, "[%d] %s", node.physicalIndex, PingPongRoleLabel(node.pingPongRole));
            }
            const bool selected = m_impl->selectedNodeId.has_value() && *m_impl->selectedNodeId == node.id;
            DrawResourceThumbnail(
                node, contentWidth, selected, matchesFilters, resourceViewRegistry, resourceActions);
        }
        const ImVec2 separatorStart = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddLine(separatorStart,
                                            ImVec2(separatorStart.x + contentWidth, separatorStart.y),
                                            ImGui::GetColorU32(ImGuiCol_Separator));
        ImGui::Dummy(ImVec2(contentWidth, 1.0f));

        if (m_impl->layoutMode == 0 && node.kind == Engine::RenderGraphNodeKind::Resource)
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
        else if (m_impl->layoutMode == 0)
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
        else if (node.kind == Engine::RenderGraphNodeKind::Resource)
        {
            const ImVec2 rowStart = ImGui::GetCursorScreenPos();
            NodeEditor::BeginPin(m_impl->ResourcePinId(node.id, Engine::RenderGraphResourceAccess::Write),
                                 NodeEditor::PinKind::Input);
            ImGui::Text("-> Write (%zu)", writePinCount);
            NodeEditor::EndPin();
            const std::string readLabel = "Read (" + std::to_string(readPinCount) + ") ->";
            const float readLabelWidth = ImGui::CalcTextSize(readLabel.c_str()).x;
            ImGui::SameLine();
            ImGui::SetCursorScreenPos(ImVec2(rowStart.x + contentWidth - readLabelWidth, rowStart.y));
            NodeEditor::BeginPin(m_impl->ResourcePinId(node.id, Engine::RenderGraphResourceAccess::Read),
                                 NodeEditor::PinKind::Output);
            ImGui::TextUnformatted(readLabel.c_str());
            NodeEditor::EndPin();
        }
        else
        {
            const size_t rowCount = (std::max)(inputPins.size(), outputPins.size());
            for (size_t row = 0; row < rowCount; ++row)
            {
                const ImVec2 rowStart = ImGui::GetCursorScreenPos();
                if (row < inputPins.size())
                {
                    const std::string label = "-> " + ShortResourceName(RelatedResourceName(document, *inputPins[row]));
                    NodeEditor::BeginPin(ToPinId(inputPins[row]->id), NodeEditor::PinKind::Input);
                    ImGui::TextUnformatted(label.c_str());
                    NodeEditor::EndPin();
                }
                else
                {
                    ImGui::Dummy(ImVec2(1.0f, ImGui::GetTextLineHeight()));
                }
                if (row < outputPins.size())
                {
                    const std::string label =
                        ShortResourceName(RelatedResourceName(document, *outputPins[row])) + " ->";
                    const float labelWidth = ImGui::CalcTextSize(label.c_str()).x;
                    ImGui::SameLine();
                    ImGui::SetCursorScreenPos(ImVec2(rowStart.x + contentWidth - labelWidth, rowStart.y));
                    NodeEditor::BeginPin(ToPinId(outputPins[row]->id), NodeEditor::PinKind::Output);
                    ImGui::TextUnformatted(label.c_str());
                    NodeEditor::EndPin();
                }
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
        if (m_impl->layoutMode == 1)
        {
            NodeEditor::PopStyleVar();
        }
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
        if (m_impl->connectedOnly && !matchesFilters)
        {
            continue;
        }
        ImVec4 color = link.access == Engine::RenderGraphResourceAccess::Read ? ImVec4(0.35f, 0.70f, 1.0f, 1.0f)
                                                                              : ImVec4(1.0f, 0.65f, 0.25f, 1.0f);
        bool snapshotColor = false;
        if (snapshotDiff.has_value() && ContainsId(snapshotDiff->addedLinks, link.id))
        {
            color = ImVec4(0.25f, 1.0f, 0.40f, 1.0f);
            snapshotColor = true;
        }
        else if (snapshotDiff.has_value() && ContainsId(snapshotDiff->changedLinks, link.id))
        {
            color = ImVec4(1.0f, 0.75f, 0.20f, 1.0f);
            snapshotColor = true;
        }
        if (!snapshotColor)
        {
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
        }
        color.w = matchesFilters ? 1.0f : 0.10f;
        const NodeEditor::PinId resourcePinId = m_impl->ResourcePinId(link.resourceNodeId, link.access);
        const NodeEditor::PinId fromPinId =
            link.access == Engine::RenderGraphResourceAccess::Read ? resourcePinId : ToPinId(link.fromPinId);
        const NodeEditor::PinId toPinId =
            link.access == Engine::RenderGraphResourceAccess::Read ? ToPinId(link.toPinId) : resourcePinId;
        NodeEditor::Link(ToLinkId(link.id), fromPinId, toPinId, color, 2.0f);
    }

    const NodeEditor::NodeId doubleClickedNodeId = NodeEditor::GetDoubleClickedNode();

    if (doubleClickedNodeId)
    {
        const Engine::RenderGraphDocumentNode* doubleClickedNode = FindNode(document, doubleClickedNodeId);
        if (doubleClickedNode != nullptr)
        {
            m_impl->selectedNodeId = doubleClickedNode->id;
            if (doubleClickedNode->kind == Engine::RenderGraphNodeKind::Resource &&
                doubleClickedNode->resourceKind == Engine::RenderGraphResourceKind::Buffer)
            {
                m_impl->bufferInspectorNodeId = doubleClickedNode->id;
                m_impl->bufferInspectorOpen = true;
                m_impl->bufferInspectorFocusRequested = true;
            }
            const Engine::DebugResourceInspection inspection =
                InspectResource(*doubleClickedNode, resourceViewRegistry);
            if (CanOpenPreview(inspection, resourceActions))
            {
                resourceActions->openPreview(*inspection.descriptor, false);
            }
        }
    }

    NodeEditor::Suspend();
    NodeEditor::NodeId contextNodeId;
    if (NodeEditor::ShowNodeContextMenu(&contextNodeId))
    {
        m_impl->selectedNodeId =
            FindNode(document, contextNodeId) != nullptr
                ? std::optional<Engine::RenderGraphDocumentId>(FindNode(document, contextNodeId)->id)
                : std::nullopt;
        ImGui::OpenPopup("RenderGraphNodeContextMenu");
    }
    if (ImGui::BeginPopup("RenderGraphNodeContextMenu"))
    {
        const Engine::RenderGraphDocumentNode* contextNode =
            m_impl->selectedNodeId.has_value() ? FindNode(document, *m_impl->selectedNodeId) : nullptr;
        if (contextNode != nullptr)
        {
            ImGui::TextUnformatted(NodeDisplayName(*contextNode));
            ImGui::Separator();
            if (contextNode->kind == Engine::RenderGraphNodeKind::Resource)
            {
                const Engine::DebugResourceInspection inspection = InspectResource(*contextNode, resourceViewRegistry);
                if (contextNode->resourceKind == Engine::RenderGraphResourceKind::Buffer &&
                    ImGui::MenuItem("Inspect Buffer"))
                {
                    m_impl->bufferInspectorNodeId = contextNode->id;
                    m_impl->bufferInspectorOpen = true;
                    m_impl->bufferInspectorFocusRequested = true;
                }
                const bool canOpen = CanOpenPreview(inspection, resourceActions);
                const bool previewOpen = resourceActions != nullptr && resourceActions->isPreviewOpen &&
                                         resourceActions->isPreviewOpen(contextNode->name);
                ImGui::BeginDisabled(!canOpen);
                if (ImGui::MenuItem("Preview"))
                {
                    resourceActions->openPreview(*inspection.descriptor, false);
                }
                if (ImGui::MenuItem("Pin Preview"))
                {
                    resourceActions->openPreview(*inspection.descriptor, true);
                }
                ImGui::EndDisabled();
                ImGui::BeginDisabled(!previewOpen || resourceActions == nullptr || !resourceActions->closePreview);
                if (ImGui::MenuItem("Close Preview"))
                {
                    resourceActions->closePreview(contextNode->name);
                }
                ImGui::EndDisabled();
                if (ImGui::MenuItem("Copy Resource Name"))
                {
                    ImGui::SetClipboardText(contextNode->name.c_str());
                }
                if (!inspection.IsInspectable())
                {
                    ImGui::TextDisabled("%s", inspection.unsupportedReason.c_str());
                }
            }
            else if (ImGui::MenuItem("Copy Node Name"))
            {
                ImGui::SetClipboardText(contextNode->name.c_str());
            }

            ImGui::Separator();
            ImVec4 indexColor = m_impl->IndexColor(*contextNode);
            if (ImGui::ColorEdit4(
                    "Index Color", &indexColor.x, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaPreviewHalf))
            {
                m_impl->indexColors[contextNode->id.value] = indexColor;
            }
            if (ImGui::IsItemDeactivatedAfterEdit())
            {
                SaveIndexColors(m_impl->metadataPath, m_impl->indexColors);
            }
            if (ImGui::MenuItem("Reset Index Color"))
            {
                m_impl->indexColors.erase(contextNode->id.value);
                SaveIndexColors(m_impl->metadataPath, m_impl->indexColors);
            }
        }
        ImGui::EndPopup();
    }
    NodeEditor::Resume();
    NodeEditor::End();
    if (focusRequested)
    {
        for (const Engine::RenderGraphDocumentNode& node : document.nodes)
        {
            if (m_impl->MatchesFilters(document, node))
            {
                NodeEditor::ClearSelection();
                NodeEditor::SelectNode(ToNodeId(node.id));
                NodeEditor::NavigateToSelection(false, 0.25f);
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
    if (fitRequested || deferredFitRequested || isolateRelatedChanged || layoutChanged ||
        previousLayoutMode != m_impl->layoutMode)
    {
        NodeEditor::NavigateToContent(0.0f);
    }
    else if (focusSelectedRequested)
    {
        NodeEditor::NavigateToSelection(false, 0.25f);
    }
    NodeEditor::SetCurrentEditor(nullptr);
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("RenderGraphDetailPane", ImVec2(0.0f, contentSize.y), true);
    constexpr float nodeDetailsHeight = 420.0f;
    ImGui::BeginChild("RenderGraphNodeDetailsPane", ImVec2(0.0f, nodeDetailsHeight), false);
    const std::optional<Engine::RenderGraphDocumentId> bufferInspectorRequest = DrawDetailPanel(document,
                                                                                                m_impl->selectedNodeId,
                                                                                                m_impl->passTimings,
                                                                                                m_impl->totalTiming,
                                                                                                m_impl->timingMode,
                                                                                                resourceViewRegistry,
                                                                                                resourceActions,
                                                                                                technologyMetadata,
                                                                                                &m_impl->connectedOnly,
                                                                                                &m_impl->fitGraphRequested);
    ImGui::EndChild();
    const std::optional<Engine::RenderGraphDocumentId> timelineSelection =
        DrawLifetimeTimeline(document, m_impl->selectedNodeId);
    DrawStateDiagnostics(document, stateDiagnostics, barrierDiagnostics);
    const std::optional<Engine::RenderGraphDocumentId> validationSelection = DrawValidationMessages(validationMessages);
    ImGui::EndChild();

    const std::optional<Engine::RenderGraphDocumentId> requestedSelection =
        validationSelection.has_value() ? validationSelection : timelineSelection;
    if (requestedSelection.has_value())
    {
        m_impl->selectedNodeId = requestedSelection;
        NodeEditor::SetCurrentEditor(m_impl->Context());
        NodeEditor::ClearSelection();
        NodeEditor::SelectNode(ToNodeId(*requestedSelection));
        NodeEditor::NavigateToSelection(false, 0.25f);
        NodeEditor::SetCurrentEditor(nullptr);
    }
    if (bufferInspectorRequest.has_value())
    {
        m_impl->bufferInspectorNodeId = bufferInspectorRequest;
        m_impl->bufferInspectorOpen = true;
        m_impl->bufferInspectorFocusRequested = true;
    }
    if (m_impl->bufferInspectorOpen && m_impl->bufferInspectorNodeId.has_value())
    {
        DrawBufferInspectorWindow(document,
                                  *m_impl->bufferInspectorNodeId,
                                  resourceViewRegistry,
                                  resourceActions,
                                  m_impl->bufferInspectorFocusRequested,
                                  m_impl->bufferInspectorOpen);
        m_impl->bufferInspectorFocusRequested = false;
    }
}
} // namespace RtPbrSurvey
