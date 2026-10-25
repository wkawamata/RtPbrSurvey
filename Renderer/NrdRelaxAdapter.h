#pragma once

#include <array>
#include <memory>
#include <span>
#include <cstdint>

namespace Engine
{

class Scene;
struct SceneMesh;
struct InstanceData;

struct NrdRelaxWorldBounds
{
    bool complete = false;
    std::array<double, 3> minimum = {};
    std::array<double, 3> maximum = {};
};

NrdRelaxWorldBounds BuildNrdRelaxWorldBounds(const Scene& scene);

struct NrdRelaxRayRangeContext
{
    const NrdRelaxWorldBounds* bounds = nullptr;
    std::array<float, 3> origin = {};
    std::array<float, 3> direction = {};
    bool geometrySnapshotMatches = false;
    const uint64_t* currentSnapshotToken = nullptr;
    uint64_t expectedSnapshotToken = 0;
};

struct NrdRelaxGpuSceneRange
{
    std::array<float, 3> minimum = {};
    std::array<float, 3> maximum = {};
    bool valid = false;
};

class NrdRelaxGeometrySnapshot
{
public:
    NrdRelaxGeometrySnapshot() = default;
    NrdRelaxGeometrySnapshot(const NrdRelaxGeometrySnapshot&) = delete;
    NrdRelaxGeometrySnapshot& operator=(const NrdRelaxGeometrySnapshot&) = delete;
    const SceneMesh& CaptureUpload(const SceneMesh& mesh);
    void RecordTlas(std::span<const InstanceData> instances);
    void Invalidate();
    void Reset();
    uint64_t Token() const;
    NrdRelaxGpuSceneRange ExportGpuSceneRange(uint64_t expectedToken) const;
    NrdRelaxRayRangeContext
    MakeRangeContext(uint64_t token, std::array<float, 3> origin, std::array<float, 3> direction) const;

private:
    std::shared_ptr<const SceneMesh> m_uploadedMesh;
    NrdRelaxWorldBounds m_bounds;
    uint64_t m_token = 0;
};

enum class NrdRelaxSupportStatus
{
    NotIntegrated = 0,
};

struct NrdRelaxSupportInfo
{
    bool available = false;
    NrdRelaxSupportStatus status = NrdRelaxSupportStatus::NotIntegrated;
    const char* targetSdkVersion = nullptr;
    const char* reason = nullptr;
};

NrdRelaxSupportInfo QueryNrdRelaxSupport();

enum class NrdRelaxPresentationStatus
{
    Native,
    NotBuilt,
    BackendUnavailable,
    IncompatibleInputs,
    AwaitingEvaluation,
    Relax,
    UnsupportedTransmission,
    UnsupportedAlphaMask,
};

NrdRelaxPresentationStatus
ResolveNrdRelaxPresentation(bool requested, bool built, bool backendReady, bool inputsCompatible, bool outputValid);
const char* NrdRelaxPresentationStatusText(NrdRelaxPresentationStatus status);

#ifdef RTPBRSURVEY_NRD_RECONSTRUCTION_DIAGNOSTICS
std::array<unsigned char, 768> MakeNrdReconstructionConstants(unsigned int width, unsigned int height);
#endif

struct NrdRelaxSamplingReadiness
{
    bool ready = false;
    const char* reason = nullptr;
};

NrdRelaxSamplingReadiness QueryNrdRelaxSamplingReadiness(unsigned int samplesPerFrame, bool pixelCenterPrimary);

enum class NrdRelaxDistanceDisposition
{
    InvalidData,
    HitCandidate,
    EnvironmentMissPolicyRequired,
    ReplacementRayRequired,
    TraceRequired,
    ExcludePrimaryMiss,
    InactiveLobe,
    PackingRangeExceeded,
    EnvironmentMissCandidate,
};

struct NrdRelaxDistanceDecision
{
    NrdRelaxDistanceDisposition disposition = NrdRelaxDistanceDisposition::InvalidData;
    float hitDistance = 0.0f;
    bool zeroRadianceRequired = false;
};

enum class NrdRelaxRadianceDisposition
{
    InvalidData,
    Packable,
    PackingRangeExceeded,
    InactiveLobe,
};

struct NrdRelaxRadianceDecision
{
    NrdRelaxRadianceDisposition disposition = NrdRelaxRadianceDisposition::InvalidData;
    std::array<float, 3> demodulated = {};
};

NrdRelaxRadianceDecision
InspectNrdRelaxRadiance(std::array<float, 3> physical, std::array<float, 3> materialFactor, bool lobeActive);

NrdRelaxDistanceDecision InspectNrdRelaxDistance(float surfaceDistance,
                                                 float diagnosticState,
                                                 float rawHitT,
                                                 float reserved,
                                                 float rayTMax,
                                                 bool primaryHit,
                                                 bool lobeActive,
                                                 const NrdRelaxRayRangeContext* rangeContext = nullptr);

struct NrdRelaxLobeInput
{
    std::array<float, 3> physical = {};
    std::array<float, 3> materialFactor = {};
    std::array<float, 4> distance = {};
    bool active = true;
    const NrdRelaxRayRangeContext* rangeContext = nullptr;
};

enum class NrdRelaxPixelDisposition
{
    InvalidData,
    PackingRangeExceeded,
    SnapshotRequired,
    TraceRequired,
    EnvironmentPolicyRequired,
    ExcludePrimaryMiss,
    ReconstructionRequired,
    Convertible,
};

struct NrdRelaxPixelDecision
{
    NrdRelaxPixelDisposition disposition = NrdRelaxPixelDisposition::InvalidData;
    std::array<std::array<float, 4>, 2> packed = {};
};

NrdRelaxPixelDecision
InspectNrdRelaxPixel(const std::array<NrdRelaxLobeInput, 2>& lobes, float rayTMax, bool primaryHit);

} // namespace Engine
