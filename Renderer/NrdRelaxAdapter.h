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
    NrdRelaxRayRangeContext MakeRangeContext(uint64_t token,
                                            std::array<float, 3> origin,
                                            std::array<float, 3> direction) const;

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

NrdRelaxDistanceDecision InspectNrdRelaxDistance(float surfaceDistance,
                                                 float diagnosticState,
                                                 float rawHitT,
                                                 float reserved,
                                                 float rayTMax,
                                                 bool primaryHit,
                                                 bool lobeActive,
                                                 const NrdRelaxRayRangeContext* rangeContext = nullptr);

} // namespace Engine
