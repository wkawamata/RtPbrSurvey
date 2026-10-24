#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <dxgiformat.h>

namespace Engine
{

enum class PathTracingInputSignal : uint32_t
{
    FrameRadiance,
    NormalRoughness,
    ViewZ,
    MotionVectors,
    BaseColor,
    MaterialF0Metallic,
    PhysicalDiffuse,
    PhysicalSpecular,
    StableRadiance,
    BackgroundRadiance,
    PrimaryPosition,
    DiffuseDirection,
    SpecularDirection,
    DiffuseDistance,
    SpecularDistance,
    // Actual radiance continuation, distinct from independent NRD lobe probes.
    PrimaryContinuation,
    SecondaryRayData,
    SecondarySurfaceDistance,
    Count,
};

inline constexpr std::size_t kPathTracingInputSignalCount = static_cast<std::size_t>(PathTracingInputSignal::Count);

// A description of producer resources, not a backend-ready resource bundle.
// Populate format and size from the actual resource, and provenance at production.
struct PathTracingInputMetadata
{
    bool present = false;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    uint32_t width = 0;
    uint32_t height = 0;
    uint64_t frameId = 0;
    uint32_t sampleStartIndex = 0;
    uint32_t samplesPerFrame = 0;
    uint64_t samplingSignature = 0;
};

struct PathTracingReconstructionFrame
{
    uint64_t frameId = 0;
    uint32_t renderWidth = 0;
    uint32_t renderHeight = 0;
    uint32_t outputWidth = 0;
    uint32_t outputHeight = 0;
    uint32_t sampleStartIndex = 0;
    uint32_t samplesPerFrame = 1;
    bool pixelCenterPrimary = false;
    // Nonzero identity for the producer sampling policy, including seed/profile.
    uint64_t samplingSignature = 0;
    // v1 uses scene-referred radiance and no projection jitter.
    float preExposure = 1.0f;
    float exposureScale = 1.0f;
    std::array<float, 2> jitterPixels = {};
    // Engine row-vector convention, row-major storage, no projection jitter.
    std::array<float, 16> worldToView = {};
    std::array<float, 16> viewToClip = {};
    // Exact previous combined transform used by the producer's motion shader.
    // Backend-specific separated previous matrices belong to its history state.
    std::array<float, 16> previousViewProjection = {};
};

struct PathTracingReconstructionInputs
{
    PathTracingReconstructionFrame frame;
    std::array<PathTracingInputMetadata, kPathTracingInputSignalCount> signals = {};
};

enum class PathTracingInputIssue
{
    None,
    InvalidDimensions,
    UnsupportedSampling,
    UnsupportedExposure,
    InvalidCameraMatrix,
    MissingRequiredSignal,
    InvalidFormat,
    ResourceSizeMismatch,
    FrameMismatch,
    SampleMismatch,
    SamplingPolicyMismatch,
    MissingProduction,
    ResourceIdentityMismatch,
    InvalidResourceShape,
};

struct PathTracingInputValidation
{
    PathTracingInputIssue issue = PathTracingInputIssue::None;
    PathTracingInputSignal signal = PathTracingInputSignal::Count;
    bool Passed() const
    {
        return issue == PathTracingInputIssue::None;
    }
};

// Checks only the common v1 metadata contract. Backend capability/pixel-content
// checks, distance admission and last-evaluation history matching remain separate.
PathTracingInputValidation ValidatePathTracingReconstructionInputs(const PathTracingReconstructionInputs& inputs);

// Previous NDC minus current NDC -> previous UV minus current UV (top-down).
std::array<float, 2> PathTracingMotionNdcToUv(const std::array<float, 2>& motion);

} // namespace Engine
