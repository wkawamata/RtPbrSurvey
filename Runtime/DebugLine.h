#pragma once

#include <DirectXMath.h>
#include <array>
#include <cstdint>
#include <vector>

namespace RtPbrSurvey
{

using DebugLineHandle = uint32_t;
static constexpr DebugLineHandle kInvalidDebugLineHandle = 0;
static constexpr uint32_t kMaxHostDebugLines = 1024;

enum class DebugLineDepthMode
{
    DepthTested,
    Overlay,
};

struct DebugLineDesc
{
    DirectX::XMFLOAT3 start = {};
    DirectX::XMFLOAT3 end = {};
    DirectX::XMFLOAT4 color = {1.0f, 1.0f, 1.0f, 1.0f};
    bool visible = true;
    DebugLineDepthMode depthMode = DebugLineDepthMode::DepthTested;
};

struct DebugLineVertex
{
    DirectX::XMFLOAT3 position;
    DirectX::XMFLOAT4 color;
};

struct DebugLineVertexGroups
{
    std::vector<DebugLineVertex> depthTested;
    std::vector<DebugLineVertex> overlay;
};

class DebugLineRegistry
{
public:
    DebugLineHandle Add(const DebugLineDesc& desc);
    bool Update(DebugLineHandle handle, const DebugLineDesc& desc);
    void Remove(DebugLineHandle handle);
    void Clear();
    DebugLineVertexGroups AssembleVertices() const;

private:
    struct Slot
    {
        DebugLineDesc desc;
        uint32_t generation = 1;
        bool occupied = false;
    };

    static DebugLineHandle MakeHandle(uint32_t index, uint32_t generation);
    static bool DecodeHandle(DebugLineHandle handle, uint32_t& index, uint32_t& generation);
    Slot* Find(DebugLineHandle handle);

    std::array<Slot, kMaxHostDebugLines> m_slots = {};
};

} // namespace RtPbrSurvey
