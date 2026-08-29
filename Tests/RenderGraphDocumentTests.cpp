#include "stdafx.h"

#include "Engine/FrameGraph/RenderGraphDocument.h"

#include <algorithm>
#include <iostream>
#include <string>
#include <unordered_set>

namespace
{
bool Check(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "FAILED: " << message << '\n';
    }
    return condition;
}

Engine::RenderGraphDocument MakeDocument()
{
    std::vector<Engine::RenderPass> passes = {
        {.name = L"GBuffer",
         .reads = {{"Depth", D3D12_RESOURCE_STATE_DEPTH_READ}},
         .writes = {{"Albedo", D3D12_RESOURCE_STATE_RENDER_TARGET}}},
        {.name = L"Lighting",
         .reads = {{"Albedo", D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE}},
         .writes = {{"SceneColor", D3D12_RESOURCE_STATE_RENDER_TARGET}}},
    };
    const Engine::RenderGraphResourceMetadataMap metadata = {
        {"Albedo", {Engine::RenderGraphResourceLifetimeKind::Transient, Engine::RenderGraphResourceKind::Texture}},
        {"SceneColor", {Engine::RenderGraphResourceLifetimeKind::Persistent, Engine::RenderGraphResourceKind::Buffer}},
    };
    return Engine::BuildRenderGraphDocument(passes, metadata);
}

bool TestDocumentTopology()
{
    const Engine::RenderGraphDocument document = MakeDocument();
    bool passed = true;
    passed &= Check(document.nodes.size() == 5, "document contains two pass and three resource nodes");
    passed &= Check(document.pins.size() == 8, "each usage creates two pins");
    passed &= Check(document.links.size() == 4, "each usage creates one link");

    const auto albedo = std::find_if(document.nodes.begin(),
                                     document.nodes.end(),
                                     [](const Engine::RenderGraphDocumentNode& node) { return node.name == "Albedo"; });
    const auto sceneColor =
        std::find_if(document.nodes.begin(),
                     document.nodes.end(),
                     [](const Engine::RenderGraphDocumentNode& node) { return node.name == "SceneColor"; });
    passed &= Check(albedo != document.nodes.end() && albedo->resourceKind == Engine::RenderGraphResourceKind::Texture,
                    "texture resource kind is preserved");
    passed &=
        Check(sceneColor != document.nodes.end() && sceneColor->resourceKind == Engine::RenderGraphResourceKind::Buffer,
              "buffer resource kind is preserved");

    std::unordered_set<uint64_t> ids;
    for (const Engine::RenderGraphDocumentNode& node : document.nodes)
    {
        passed &= Check(node.id.value != 0, "node id is valid");
        passed &= Check(ids.insert(node.id.value).second, "node ids are unique");
    }
    for (const Engine::RenderGraphDocumentPin& pin : document.pins)
    {
        passed &= Check(pin.id.value != 0, "pin id is valid");
        passed &= Check(ids.insert(pin.id.value).second, "pin ids are unique");
    }
    for (const Engine::RenderGraphDocumentLink& link : document.links)
    {
        passed &= Check(link.id.value != 0, "link id is valid");
        passed &= Check(ids.insert(link.id.value).second, "link ids are unique");
    }
    return passed;
}

bool TestDeterministicDump()
{
    const Engine::RenderGraphDocument first = MakeDocument();
    const Engine::RenderGraphDocument second = MakeDocument();
    const std::string text = Engine::DumpRenderGraphDocumentText(first);
    const std::string dot = Engine::DumpRenderGraphDocumentDot(first);

    bool passed = true;
    passed &= Check(text == Engine::DumpRenderGraphDocumentText(second), "text dump is deterministic");
    passed &= Check(dot == Engine::DumpRenderGraphDocumentDot(second), "DOT dump is deterministic");
    passed &= Check(text.find("Pass [0] GBuffer") != std::string::npos, "text dump contains pass order");
    passed &= Check(text.find("Write Albedo state=RENDER_TARGET lifetime=[0,1] kind=transient") != std::string::npos,
                    "text dump contains resource state and lifetime");
    passed &= Check(dot.find("digraph RenderGraph") != std::string::npos, "DOT dump contains graph header");
    passed &= Check(dot.find("read PIXEL_SHADER_RESOURCE") != std::string::npos, "DOT dump contains read edge state");
    return passed;
}

bool TestPingPongNodeIdentity()
{
    const std::vector<Engine::RenderPass> evenPasses = {
        {.name = L"TemporalReflectionPass",
         .reads = {{"ReflectionHistoryDepth.0", D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE}},
         .writes = {{"ReflectionHistoryDepth.1", D3D12_RESOURCE_STATE_RENDER_TARGET}}},
    };
    const std::vector<Engine::RenderPass> oddPasses = {
        {.name = L"TemporalReflectionPass",
         .reads = {{"ReflectionHistoryDepth.1", D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE}},
         .writes = {{"ReflectionHistoryDepth.0", D3D12_RESOURCE_STATE_RENDER_TARGET}}},
    };

    const Engine::RenderGraphResourceMetadataMap evenMetadata = {
        {"ReflectionHistoryDepth.0",
         {Engine::RenderGraphResourceLifetimeKind::Persistent,
          Engine::RenderGraphResourceKind::Texture,
          "ReflectionHistoryDepth",
          0,
          Engine::RenderGraphPingPongRole::HistoryRead}},
        {"ReflectionHistoryDepth.1",
         {Engine::RenderGraphResourceLifetimeKind::Persistent,
          Engine::RenderGraphResourceKind::Texture,
          "ReflectionHistoryDepth",
          1,
          Engine::RenderGraphPingPongRole::CurrentWrite}},
    };
    const Engine::RenderGraphResourceMetadataMap oddMetadata = {
        {"ReflectionHistoryDepth.0",
         {Engine::RenderGraphResourceLifetimeKind::Persistent,
          Engine::RenderGraphResourceKind::Texture,
          "ReflectionHistoryDepth",
          0,
          Engine::RenderGraphPingPongRole::CurrentWrite}},
        {"ReflectionHistoryDepth.1",
         {Engine::RenderGraphResourceLifetimeKind::Persistent,
          Engine::RenderGraphResourceKind::Texture,
          "ReflectionHistoryDepth",
          1,
          Engine::RenderGraphPingPongRole::HistoryRead}},
    };
    const Engine::RenderGraphDocument even = Engine::BuildRenderGraphDocument(evenPasses, evenMetadata);
    const Engine::RenderGraphDocument odd = Engine::BuildRenderGraphDocument(oddPasses, oddMetadata);
    bool passed = true;
    passed &= Check(even.nodes.size() == odd.nodes.size(), "ping-pong frames keep the same node count");
    for (size_t nodeIndex = 0; nodeIndex < even.nodes.size() && nodeIndex < odd.nodes.size(); ++nodeIndex)
    {
        passed &= Check(even.nodes[nodeIndex].name == odd.nodes[nodeIndex].name,
                        "ping-pong nodes keep deterministic name order");
        passed &=
            Check(even.nodes[nodeIndex].id == odd.nodes[nodeIndex].id, "ping-pong nodes keep stable document IDs");
        if (even.nodes[nodeIndex].kind == Engine::RenderGraphNodeKind::Resource)
        {
            passed &= Check(even.nodes[nodeIndex].logicalGroupId == odd.nodes[nodeIndex].logicalGroupId,
                            "ping-pong resources keep a stable logical group ID");
            passed &= Check(even.nodes[nodeIndex].logicalGroupName == "ReflectionHistoryDepth",
                            "ping-pong resources preserve the logical group name");
            passed &= Check(even.nodes[nodeIndex].physicalIndex == odd.nodes[nodeIndex].physicalIndex,
                            "ping-pong resources keep their physical index");
            passed &= Check(even.nodes[nodeIndex].pingPongRole != odd.nodes[nodeIndex].pingPongRole,
                            "ping-pong resources exchange only their current role");
        }
    }
    passed &= Check(even.links.size() == odd.links.size(), "ping-pong frames keep the same link count");
    passed &= Check(even.links.front().id != odd.links.front().id,
                    "ping-pong read/write role changes are represented by dynamic links");
    return passed;
}

bool TestStateFormatting()
{
    bool passed = true;
    passed &=
        Check(Engine::FormatD3D12ResourceStates(D3D12_RESOURCE_STATE_COMMON) == "COMMON", "COMMON state is formatted");
    passed &= Check(Engine::FormatD3D12ResourceStates(D3D12_RESOURCE_STATE_GENERIC_READ) == "GENERIC_READ",
                    "GENERIC_READ state is formatted as an alias");
    const D3D12_RESOURCE_STATES combined = static_cast<D3D12_RESOURCE_STATES>(
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    passed &= Check(Engine::FormatD3D12ResourceStates(combined) == "NON_PIXEL_SHADER_RESOURCE|PIXEL_SHADER_RESOURCE",
                    "combined states are formatted symbolically");
    return passed;
}

bool TestStateDiagnostics()
{
    const std::vector<Engine::RenderPass> passes = {
        {.name = L"WriteColor", .writes = {{"Color", D3D12_RESOURCE_STATE_RENDER_TARGET}}},
        {.name = L"ReadColor", .reads = {{"Color", D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE}}},
        {.name = L"WriteUav", .writes = {{"Counters", D3D12_RESOURCE_STATE_UNORDERED_ACCESS}}},
        {.name = L"ReadUav", .reads = {{"Counters", D3D12_RESOURCE_STATE_UNORDERED_ACCESS}}},
        {.name = L"ReadStable", .reads = {{"Stable", D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE}}},
        {.name = L"ReadStableAgain", .reads = {{"Stable", D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE}}},
    };
    const Engine::RenderGraphDocument document = Engine::BuildRenderGraphDocument(passes);
    const std::vector<Engine::RenderGraphStateDiagnostic> diagnostics =
        Engine::BuildRenderGraphStateDiagnostics(document);

    bool passed = true;
    passed &= Check(diagnostics.size() == 2, "state diagnostics contain transition and UAV candidate only");
    const auto transition =
        std::find_if(diagnostics.begin(),
                     diagnostics.end(),
                     [](const auto& diagnostic)
                     { return diagnostic.kind == Engine::RenderGraphStateDiagnosticKind::RequiredTransition; });
    const auto uav =
        std::find_if(diagnostics.begin(),
                     diagnostics.end(),
                     [](const auto& diagnostic)
                     { return diagnostic.kind == Engine::RenderGraphStateDiagnosticKind::UavBarrierCandidate; });
    passed &= Check(transition != diagnostics.end(), "changed state is a required transition");
    passed &= Check(transition != diagnostics.end() && transition->beforeState == D3D12_RESOURCE_STATE_RENDER_TARGET &&
                        transition->afterState == D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                    "required transition preserves before and after states");
    passed &= Check(uav != diagnostics.end(), "consecutive UAV usage after write is a UAV barrier candidate");
    return passed;
}

bool TestSnapshotSerializationAndDiff()
{
    Engine::RenderGraphSnapshot snapshot;
    snapshot.metadata.label = "baseline";
    snapshot.metadata.rendererMode = "deferred";
    snapshot.metadata.features["hybridReflection"] = true;
    snapshot.document = MakeDocument();

    const std::string serialized = Engine::SerializeRenderGraphSnapshot(snapshot);
    Engine::RenderGraphSnapshot restored;
    std::string error;
    bool passed = true;
    passed &= Check(Engine::DeserializeRenderGraphSnapshot(serialized, restored, error),
                    "snapshot round-trip deserializes");
    passed &= Check(Engine::SerializeRenderGraphSnapshot(restored) == serialized,
                    "snapshot round-trip is byte-identical");

    Engine::RenderGraphSnapshot shuffled = snapshot;
    std::reverse(shuffled.document.nodes.begin(), shuffled.document.nodes.end());
    std::reverse(shuffled.document.pins.begin(), shuffled.document.pins.end());
    std::reverse(shuffled.document.links.begin(), shuffled.document.links.end());
    passed &= Check(Engine::SerializeRenderGraphSnapshot(shuffled) == serialized,
                    "snapshot serialization is independent of input order");
    passed &= Check(!Engine::DiffRenderGraphDocuments(snapshot.document, restored.document).HasChanges(),
                    "identical snapshot diff is empty");

    Engine::RenderGraphDocument changed = snapshot.document;
    changed.nodes.front().firstPass += 1;
    changed.links.front().state = D3D12_RESOURCE_STATE_COPY_SOURCE;
    changed.nodes.push_back({{0x123456789abcdef0ull}, Engine::RenderGraphNodeKind::Resource, "Added"});
    changed.links.pop_back();
    const Engine::RenderGraphDocumentDiff diff = Engine::DiffRenderGraphDocuments(snapshot.document, changed);
    passed &= Check(diff.addedNodes.size() == 1, "snapshot diff detects added node");
    passed &= Check(diff.changedNodes.size() == 1, "snapshot diff detects changed node");
    passed &= Check(diff.changedLinks.size() == 1, "snapshot diff detects changed link");
    passed &= Check(diff.removedLinks.size() == 1, "snapshot diff detects removed link");

    Engine::RenderGraphSnapshot invalid;
    passed &= Check(!Engine::DeserializeRenderGraphSnapshot("{\"schemaVersion\":2}", invalid, error),
                    "unsupported snapshot schema is rejected");
    return passed;
}
} // namespace

int main()
{
    bool passed = true;
    passed &= TestDocumentTopology();
    passed &= TestDeterministicDump();
    passed &= TestPingPongNodeIdentity();
    passed &= TestStateFormatting();
    passed &= TestStateDiagnostics();
    passed &= TestSnapshotSerializationAndDiff();
    return passed ? 0 : 1;
}
