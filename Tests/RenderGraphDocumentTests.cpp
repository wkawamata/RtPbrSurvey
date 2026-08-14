#include "stdafx.h"

#include "Engine/FrameGraph/RenderGraphDocument.h"

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
        {"Albedo", {Engine::RenderGraphResourceLifetimeKind::Transient}},
        {"SceneColor", {Engine::RenderGraphResourceLifetimeKind::Persistent}},
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
} // namespace

int main()
{
    bool passed = true;
    passed &= TestDocumentTopology();
    passed &= TestDeterministicDump();
    passed &= TestStateFormatting();
    return passed ? 0 : 1;
}
