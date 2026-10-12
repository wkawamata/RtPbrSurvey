#include "stdafx.h"

#include "NrdRelaxAdapter.h"

#include <cmath>

namespace Engine
{

NrdRelaxSupportInfo QueryNrdRelaxSupport()
{
    NrdRelaxSupportInfo info;
    info.targetSdkVersion = "4.17.3";
    info.reason = "NRD RELAX dispatch and PT input conversion are not integrated.";
    return info;
}

NrdRelaxSamplingReadiness QueryNrdRelaxSamplingReadiness(unsigned int samplesPerFrame, bool pixelCenterPrimary)
{
    if (samplesPerFrame != 1)
    {
        return {false, "RELAX prototype requires exactly one primary sample per pixel/frame."};
    }
    if (!pixelCenterPrimary)
    {
        return {false, "RELAX prototype requires pixel-center primary sampling."};
    }
    return {true, "Sampling only is coherent; packing, distances and dispatch remain unvalidated."};
}

NrdRelaxDistanceDecision InspectNrdRelaxDistance(float surfaceDistance,
                                                 float diagnosticState,
                                                 float rawHitT,
                                                 float reserved,
                                                 float rayTMax,
                                                 bool primaryHit,
                                                 bool lobeActive)
{
    NrdRelaxDistanceDecision result;
    if (!std::isfinite(surfaceDistance) || !std::isfinite(diagnosticState) || !std::isfinite(rawHitT) ||
        !std::isfinite(reserved) || !std::isfinite(rayTMax) || surfaceDistance < 0.0f || rawHitT < 0.0f ||
        reserved != 0.0f || rayTMax <= 0.0f)
    {
        return result;
    }
    if (diagnosticState == 0.0f)
    {
        if (surfaceDistance == 0.0f && rawHitT == 0.0f)
        {
            result.disposition = primaryHit ? NrdRelaxDistanceDisposition::TraceRequired
                                            : NrdRelaxDistanceDisposition::ExcludePrimaryMiss;
        }
        return result;
    }
    if (!primaryHit)
    {
        return result;
    }
    if (diagnosticState == 4.0f)
    {
        if (!lobeActive && surfaceDistance == 0.0f && rawHitT == 0.0f)
        {
            result.disposition = NrdRelaxDistanceDisposition::InactiveLobe;
            result.zeroRadianceRequired = true;
        }
        return result;
    }
    if (!lobeActive)
    {
        return result;
    }
    if (diagnosticState == 1.0f && surfaceDistance > 0.0f && rawHitT > 0.0f && rawHitT <= rayTMax)
    {
        if (surfaceDistance > 65504.0f)
        {
            result.disposition = NrdRelaxDistanceDisposition::PackingRangeExceeded;
        }
        else
        {
            result.disposition = NrdRelaxDistanceDisposition::HitCandidate;
            result.hitDistance = surfaceDistance;
        }
    }
    else if (diagnosticState == 2.0f && surfaceDistance == 0.0f && rawHitT == rayTMax)
    {
        result.disposition = NrdRelaxDistanceDisposition::EnvironmentMissPolicyRequired;
    }
    else if (diagnosticState == 3.0f && surfaceDistance == 0.0f && rawHitT == 0.0f)
    {
        result.disposition = NrdRelaxDistanceDisposition::ReplacementRayRequired;
    }
    return result;
}

} // namespace Engine
