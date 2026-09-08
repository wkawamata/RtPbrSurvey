#pragma once

#include "Renderer/DebugResourceViewRegistry.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace RtPbrSurvey
{
struct DebugTextureThumbnailSlotPlan
{
    bool active = false;
    bool update = false;
    std::string resourceName;
    Engine::DebugTexturePreviewSemantic semantic = Engine::DebugTexturePreviewSemantic::Color;
    Engine::DebugResourceViewKind viewKind = Engine::DebugResourceViewKind::Texture;
};

class DebugTextureThumbnailScheduler
{
public:
    static constexpr size_t kSlotCount = 4;
    static constexpr uint64_t kUnselectedRefreshInterval = 8;

    void Request(const Engine::DebugResourceViewDescriptor& descriptor, bool selected);
    std::array<DebugTextureThumbnailSlotPlan, kSlotCount> BuildFramePlan(uint64_t frameIndex);
    std::optional<size_t> FindReadySlot(const std::string& resourceName) const;
    void Reset();

private:
    struct RequestState
    {
        std::string resourceName;
        Engine::DebugTexturePreviewSemantic semantic = Engine::DebugTexturePreviewSemantic::Color;
        Engine::DebugResourceViewKind viewKind = Engine::DebugResourceViewKind::Texture;
        bool selected = false;
    };

    struct SlotState
    {
        bool active = false;
        std::string resourceName;
        Engine::DebugTexturePreviewSemantic semantic = Engine::DebugTexturePreviewSemantic::Color;
        Engine::DebugResourceViewKind viewKind = Engine::DebugResourceViewKind::Texture;
        uint64_t lastUpdateFrame = 0;
        uint64_t readyFrame = UINT64_MAX;
    };

    std::vector<RequestState> m_pendingRequests;
    std::array<SlotState, kSlotCount> m_slots;
    size_t m_roundRobinCursor = 0;
    uint64_t m_currentFrame = 0;
};
} // namespace RtPbrSurvey
