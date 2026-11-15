#pragma once

#include "Engine/FrameGraph/RenderGraphDocument.h"

#include <algorithm>
#include <unordered_set>
#include <unordered_map>

namespace RtPbrSurvey
{
// Inspection only: a union of observed structures, never an executable graph
// or a source of barrier validation. Pass indices are stable observation slots.
class RenderGraphObservation
{
public:
    void Observe(const Engine::RenderGraphDocument& current)
    {
        m_current.clear();
        for (const auto& node : current.nodes)
        {
            m_current.insert(node.id.value);
            auto found = std::find_if(m_document.nodes.begin(), m_document.nodes.end(),
                [&](const auto& value) { return value.id == node.id; });
            if (found == m_document.nodes.end())
            {
                m_document.nodes.push_back(node);
                if (node.kind == Engine::RenderGraphNodeKind::Pass)
                    m_document.nodes.back().passIndex = m_nextSlot++;
            }
            else if (node.kind == Engine::RenderGraphNodeKind::Resource)
            {
                *found = node;
            }
        }
        for (const auto& link : current.links)
        {
            const auto known = std::find_if(m_document.links.begin(), m_document.links.end(), [&](const auto& value)
            {
                return value.passNodeId == link.passNodeId && value.resourceNodeId == link.resourceNodeId &&
                    value.access == link.access;
            });
            if (known == m_document.links.end())
            {
                for (const auto& pin : current.pins)
                    if (pin.id == link.fromPinId || pin.id == link.toPinId)
                        m_document.pins.push_back(pin);
                m_document.links.push_back(link);
            }
            else
            {
                // Usage-list indices can change when optional resources appear.
                // Keep the first semantic connection/pin identity and its position.
                known->state = link.state;
                for (auto& pin : m_document.pins)
                    if (pin.id == known->fromPinId || pin.id == known->toPinId)
                        pin.state = link.state;
            }
        }
        std::unordered_map<uint64_t, Engine::RenderGraphDocumentNode*> nodes;
        for (auto& node : m_document.nodes)
        {
            nodes[node.id.value] = &node;
            if (node.kind == Engine::RenderGraphNodeKind::Resource)
            {
                if (!IsCurrent(node.id))
                    node.pingPongRole = Engine::RenderGraphPingPongRole::None;
                node.firstPass = node.lastPass = -1;
            }
        }
        for (const auto& link : m_document.links)
        {
            const auto resource = nodes.find(link.resourceNodeId.value);
            const auto pass = nodes.find(link.passNodeId.value);
            if (resource == nodes.end() || pass == nodes.end())
                continue;
            auto& node = *resource->second;
            const int slot = pass->second->passIndex;
            node.firstPass = node.firstPass < 0 ? slot : (std::min)(node.firstPass, slot);
            node.lastPass = (std::max)(node.lastPass, slot);
        }
    }

    bool IsCurrent(Engine::RenderGraphDocumentId id) const { return m_current.contains(id.value); }
    const Engine::RenderGraphDocument& Document() const { return m_document; }
    void Clear() { m_document = {}; m_current.clear(); m_nextSlot = 0; }

private:
    Engine::RenderGraphDocument m_document;
    std::unordered_set<uint64_t> m_current;
    int m_nextSlot = 0;
};
}
