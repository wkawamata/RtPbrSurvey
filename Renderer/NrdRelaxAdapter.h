#pragma once

namespace Engine
{

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
                                                 bool lobeActive);

} // namespace Engine
