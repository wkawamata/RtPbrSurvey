#ifndef RTPBRSURVEY_ROUGH_VOLUME_TRANSMISSION_HLSLI
#define RTPBRSURVEY_ROUGH_VOLUME_TRANSMISSION_HLSLI

#include "VolumeTransmission.hlsli"
#include "PathTracingDenoiserSampling.hlsli"

struct RoughVolumeBoundarySample
{
    float3 direction;
    float weight;
    float pdf;
    float eventProbability;
    float fresnel;
    bool reflected;
    bool delta;
    bool valid;
};

bool ValidRoughVolumeParameters(float3 view, float roughness, float incidentIor, float transmittedIor)
{
    return all(isfinite(view)) && abs(dot(view, view) - 1.0) < 0.0002 && view.z > 0.0 &&
        isfinite(roughness) && roughness >= 0.0 && roughness <= 1.0 &&
        isfinite(incidentIor) && incidentIor > 0.0 && isfinite(transmittedIor) && transmittedIor > 0.0;
}

float RoughVolumeSmithG1(float cosine, float alpha)
{
    return 2.0 * cosine / (cosine + sqrt(alpha * alpha + (1.0 - alpha * alpha) * cosine * cosine));
}

// Local normal is +Z, both directions point away from the boundary; radiance transport.
float EvaluateRoughVolumeBoundary(float3 view, float3 direction, float roughness,
    float incidentIor, float transmittedIor, out float pdf)
{
    pdf = 0.0;
    if (!ValidRoughVolumeParameters(view, roughness, incidentIor, transmittedIor) || roughness == 0.0 ||
        incidentIor == transmittedIor || !all(isfinite(direction)) ||
        abs(dot(direction, direction) - 1.0) > 0.0002 || direction.z == 0.0)
    {
        return 0.0;
    }
    const bool reflected = direction.z > 0.0;
    const float etap = transmittedIor / incidentIor;
    const float3 halfSum = view + direction * (reflected ? 1.0 : etap);
    if (!all(isfinite(halfSum)) || dot(halfSum, halfSum) <= 0.0)
    {
        return 0.0;
    }
    float3 halfVector = normalize(halfSum);
    halfVector *= halfVector.z < 0.0 ? -1.0 : 1.0;
    const float viewDotHalf = dot(view, halfVector);
    const float directionDotHalf = dot(direction, halfVector);
    if (halfVector.z <= 0.0 || viewDotHalf <= 0.0 || directionDotHalf * direction.z <= 0.0)
    {
        return 0.0;
    }
    const VolumeBoundarySample boundary = SampleSmoothVolumeBoundary(-view, halfVector, incidentIor, transmittedIor, 0.0);
    const float alpha = max(roughness * roughness, 0.001);
    const float alphaSquared = alpha * alpha;
    const float denominatorD = dot(halfVector.xy, halfVector.xy) + alphaSquared * halfVector.z * halfVector.z;
    const float distribution = alphaSquared / (kDenoiserSamplingPi * denominatorD * denominatorD);
    const float geometry = RoughVolumeSmithG1(view.z, alpha) * RoughVolumeSmithG1(abs(direction.z), alpha);
    const float halfPdf = DenoiserGgxVisibleNormalPdf(halfVector, roughness, view);
    float value;
    if (reflected)
    {
        pdf = halfPdf * boundary.fresnel / (4.0 * viewDotHalf);
        value = boundary.fresnel * distribution * geometry / (4.0 * view.z * direction.z);
    }
    else
    {
        const float denominator = directionDotHalf + viewDotHalf / etap;
        const float denominatorSquared = denominator * denominator;
        if (denominatorSquared <= 0.0)
        {
            return 0.0;
        }
        pdf = halfPdf * (1.0 - boundary.fresnel) * abs(directionDotHalf) / denominatorSquared;
        value = (1.0 - boundary.fresnel) * distribution * geometry *
            abs(directionDotHalf * viewDotHalf / (view.z * direction.z * denominatorSquared)) / (etap * etap);
    }
    if (!isfinite(value) || !isfinite(pdf) || value < 0.0 || pdf < 0.0)
    {
        pdf = 0.0;
        return 0.0;
    }
    return value;
}

RoughVolumeBoundarySample SampleRoughVolumeBoundary(float3 incident, float roughness,
    float incidentIor, float transmittedIor, float randomEvent, float2 randomDirection)
{
    RoughVolumeBoundarySample result = (RoughVolumeBoundarySample)0;
    const float3 view = -incident;
    if (!ValidRoughVolumeParameters(view, roughness, incidentIor, transmittedIor) ||
        !ValidDenoiserRandomSample(randomDirection) || !isfinite(randomEvent) || randomEvent < 0.0 || randomEvent >= 1.0)
    {
        return result;
    }
    result.delta = roughness == 0.0 || incidentIor == transmittedIor;
    const float3 halfVector = result.delta ? float3(0, 0, 1) :
        SampleDenoiserGgxVisibleNormal(randomDirection, roughness, view);
    const VolumeBoundarySample boundary = SampleSmoothVolumeBoundary(incident, halfVector,
        incidentIor, transmittedIor, randomEvent);
    if (!boundary.valid || (boundary.reflected ? boundary.direction.z <= 0.0 : boundary.direction.z >= 0.0))
    {
        return result;
    }
    result.direction = normalize(boundary.direction);
    result.fresnel = boundary.fresnel;
    result.eventProbability = boundary.eventProbability;
    result.reflected = boundary.reflected;
    result.weight = boundary.weight;
    if (!result.delta)
    {
        result.weight *= RoughVolumeSmithG1(abs(result.direction.z), max(roughness * roughness, 0.001));
        EvaluateRoughVolumeBoundary(view, result.direction, roughness, incidentIor, transmittedIor, result.pdf);
    }
    if (!isfinite(result.weight) || result.weight < 0.0 || (!result.delta && result.pdf <= 0.0))
    {
        return (RoughVolumeBoundarySample)0;
    }
    result.valid = true;
    return result;
}

#endif
