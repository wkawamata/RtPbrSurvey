#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Engine
{
struct CameraState;
inline constexpr std::array<float, 3> kNrdScreenMotionVectorScale = {0.5f, -0.5f, 0.0f};
bool NrdCameraProjectionMatches(const CameraState& previous, const CameraState& current);
bool NrdPreviousViewProjectionMatches(const std::array<float, 16>& worldToView,
                                      const std::array<float, 16>& viewToClip,
                                      const std::array<float, 16>& storedPreviousViewProjection);

enum class NrdSdkResourceKind
{
    Motion,
    NormalRoughness,
    ViewZ,
    DiffuseInput,
    SpecularInput,
    DiffuseOutput,
    SpecularOutput,
    Transient,
    Permanent,
    Unsupported,
};

struct NrdSdkBinding
{
    NrdSdkResourceKind kind = NrdSdkResourceKind::Unsupported;
    uint16_t poolIndex = 0;
    bool storage = false;
};

struct NrdSdkDispatch
{
    std::string name;
    uint16_t pipelineIndex = 0;
    uint16_t gridWidth = 0;
    uint16_t gridHeight = 0;
    std::vector<NrdSdkBinding> bindings;
    std::vector<uint8_t> constants;
};

struct NrdPrepassSettings
{
    float diffuseRadius = 30.0f;
    float specularRadius = 50.0f;
};

struct NrdSdkDescription
{
    std::string version;
    uint32_t pipelines = 0;
    uint32_t permanentTextures = 0;
    uint32_t transientTextures = 0;
    uint32_t maximumConstantBytes = 0;
    bool embeddedDxil = false;
    NrdPrepassSettings prepass;
};

struct NrdSdkPipeline
{
    std::string identifier;
    uint32_t textures = 0;
    uint32_t storageTextures = 0;
};

struct NrdSdkTexture
{
    uint32_t dxgiFormat = 0;
    uint16_t downsampleFactor = 1;
};

struct NrdSdkLayout
{
    uint32_t constantSpace = 0;
    uint32_t resourceSpace = 0;
    uint32_t constantRegister = 0;
    uint32_t maximumDispatches = 0;
    std::vector<NrdSdkPipeline> pipelines;
    std::vector<NrdSdkTexture> permanent;
    std::vector<NrdSdkTexture> transient;
};

struct NrdSdkFrame
{
    // Column-major, non-jittered matrices, as required by NRD CommonSettings.
    std::array<float, 16> viewToClip = {};
    std::array<float, 16> previousViewToClip = {};
    std::array<float, 16> worldToView = {};
    std::array<float, 16> previousWorldToView = {};
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t previousWidth = 0;
    uint32_t previousHeight = 0;
    uint32_t frameIndex = 0;
    bool reset = true;
};

class NrdRelaxSdkAdapter
{
public:
    NrdRelaxSdkAdapter();
    ~NrdRelaxSdkAdapter();
    NrdRelaxSdkAdapter(const NrdRelaxSdkAdapter&) = delete;
    NrdRelaxSdkAdapter& operator=(const NrdRelaxSdkAdapter&) = delete;
    bool Initialize(std::string& error, const NrdPrepassSettings& prepass = {});
    void Shutdown();
    const NrdSdkDescription& Description() const;
    const NrdSdkLayout& Layout() const;
    bool PrepareFrame(const NrdSdkFrame& frame, std::vector<NrdSdkDispatch>& dispatches, std::string& error);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
    NrdSdkDescription m_description;
    NrdSdkLayout m_layout;
};
} // namespace Engine
