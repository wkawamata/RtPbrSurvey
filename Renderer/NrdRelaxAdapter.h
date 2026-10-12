#pragma once

#include <array>

namespace Engine
{

class Scene;

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
