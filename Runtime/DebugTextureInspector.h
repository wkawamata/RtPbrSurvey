#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace RtPbrSurvey
{
enum class DebugTextureSemantic
{
    Color,
    Normal,
    Depth,
    MotionVector,
    Scalar,
};

enum class DebugTextureChannel
{
    Rgba,
    R,
    G,
    B,
    A,
};

enum class DebugTextureFilter
{
    Nearest,
    Linear,
};

struct DebugTextureInspector
{
    uint64_t id = 0;
    std::string resourceName;
    std::string displayName;
    DebugTextureSemantic semantic = DebugTextureSemantic::Color;
    DebugTextureChannel channel = DebugTextureChannel::Rgba;
    DebugTextureFilter filter = DebugTextureFilter::Nearest;
    float exposure = 0.0f;
    float scale = 1.0f;
    float offset = 0.0f;
    bool pinned = false;
    bool open = true;
};

class DebugTextureInspectorManager
{
public:
    DebugTextureInspector& OpenPreview(
        std::string resourceName, std::string displayName, DebugTextureSemantic semantic);
    DebugTextureInspector& PinPreview(
        std::string resourceName, std::string displayName, DebugTextureSemantic semantic);
    bool Close(uint64_t id);
    void RemoveClosed();

    DebugTextureInspector* Find(uint64_t id);
    const DebugTextureInspector* Find(uint64_t id) const;
    std::vector<DebugTextureInspector>& Inspectors() { return m_inspectors; }
    const std::vector<DebugTextureInspector>& Inspectors() const { return m_inspectors; }

private:
    DebugTextureInspector& Create(
        std::string resourceName, std::string displayName, DebugTextureSemantic semantic, bool pinned);

    std::vector<DebugTextureInspector> m_inspectors;
    uint64_t m_nextId = 1;
};
} // namespace RtPbrSurvey
