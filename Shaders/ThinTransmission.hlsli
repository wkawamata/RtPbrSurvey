#ifndef RTPBRSURVEY_THIN_TRANSMISSION_HLSLI
#define RTPBRSURVEY_THIN_TRANSMISSION_HLSLI

#include "PathTracingSampling.hlsli"
#include "PathTracingDenoiserSampling.hlsli"

// Dielectric Fresnel equations; thin sheets keep the outgoing transmission direction unchanged.
float ThinTransmissionFresnel(float cosine, float ior)
{
    if (ior == 1.0)
    {
        return 0.0;
    }
    const float cosI = saturate(abs(cosine));
    const float sinSquaredT = (1.0 - cosI * cosI) / ior / ior;
    const float cosT = sqrt(max(0.0, 1.0 - sinSquaredT));
    const float parallel = (ior * cosI - cosT) / (ior * cosI + cosT);
    const float perpendicular = (cosI - ior * cosT) / (cosI + ior * cosT);
    return saturate(0.5 * (parallel * parallel + perpendicular * perpendicular));
}

struct ThinTransmissionSample
{
    float3 direction;
    float3 weight;
    bool reflected;
    float pdf;
    bool valid;
    bool delta;
};

ThinTransmissionSample SampleThinTransmission(float3 incident, float3 normal, float ior, float3 tint, float random)
{
    const float fresnel = ThinTransmissionFresnel(dot(-incident, normal), ior);
    ThinTransmissionSample result;
    result.reflected = random < fresnel;
    result.direction = result.reflected ? reflect(incident, normal) : incident;
    result.weight = result.reflected ? float3(1.0, 1.0, 1.0) : tint;
    result.pdf = 0.0;
    result.valid = true;
    result.delta = true;
    return result;
}

float ThinTransmissionLobeRoughness(float roughness, float ior, bool reflected)
{
    const float alphaScale = reflected ? 1.0 : saturate(2.0 * ior - 2.0);
    return roughness * sqrt(alphaScale);
}

float3 EvaluateRoughThinTransmission(float3 view, float3 direction, float3 normal,
    float roughness, float ior, float3 tint, out float pdf)
{
    pdf = 0.0;
    const float normalDotView = dot(normal, view);
    const float normalDotDirection = abs(dot(normal, direction));
    if (normalDotView <= 0.0 || normalDotDirection <= 0.0 || roughness <= 0.0 || ior == 1.0)
    {
        return 0.0;
    }
    const bool reflected = dot(normal, direction) > 0.0;
    const float3 mirroredDirection = reflected ? direction : reflect(direction, normal);
    const float3 sum = view + mirroredDirection;
    if (dot(sum, sum) <= 0.00000001)
    {
        return 0.0;
    }
    const float3 halfVector = normalize(sum);
    const float viewDotHalf = dot(view, halfVector);
    const float normalDotHalf = dot(normal, halfVector);
    if (viewDotHalf <= 0.0 || normalDotHalf <= 0.0)
    {
        return 0.0;
    }
    const float lobeRoughness = ThinTransmissionLobeRoughness(roughness, ior, reflected);
    const float alpha = max(lobeRoughness * lobeRoughness, 0.001);
    const float3 tangentHalf = halfVector - normal * normalDotHalf;
    const float denominator = dot(tangentHalf, tangentHalf) + alpha * alpha * normalDotHalf * normalDotHalf;
    const float distribution = alpha * alpha / (kPathTracingPi * denominator * denominator);
    const float gView = PathTracingSmithG1(normalDotView, alpha);
    const float gDirection = PathTracingSmithG1(normalDotDirection, alpha);
    const float selection = ThinTransmissionFresnel(normalDotView, ior);
    const float branchProbability = reflected ? selection : 1.0 - selection;
    pdf = branchProbability * distribution * gView / (4.0 * normalDotView);
    const float fresnel = ThinTransmissionFresnel(viewDotHalf, ior);
    const float3 attenuation = reflected ? fresnel.xxx : (1.0 - fresnel) * tint;
    return attenuation * distribution * gView * gDirection / (4.0 * normalDotView * normalDotDirection);
}

ThinTransmissionSample SampleRoughThinTransmission(float3 incident, float3 normal, float roughness,
    float ior, float3 tint, float branchRandom, float2 directionRandom)
{
    if (roughness <= 0.0 || ior == 1.0)
    {
        return SampleThinTransmission(incident, normal, ior, tint, branchRandom);
    }
    ThinTransmissionSample result = (ThinTransmissionSample)0;
    const float3 view = -incident;
    if (dot(view, normal) <= 0.0)
    {
        return result;
    }
    result.reflected = branchRandom < ThinTransmissionFresnel(dot(view, normal), ior);
    const float lobeRoughness = ThinTransmissionLobeRoughness(roughness, ior, result.reflected);
    float3 tangent;
    float3 bitangent;
    BuildPathTracingTangentFrame(normal, tangent, bitangent);
    const float3 localView = float3(dot(view, tangent), dot(view, bitangent), dot(view, normal));
    const float3 localHalf = SampleDenoiserGgxVisibleNormal(directionRandom, lobeRoughness, localView);
    const float3 halfVector = tangent * localHalf.x + bitangent * localHalf.y + normal * localHalf.z;
    const float3 reflection = reflect(incident, halfVector);
    if (dot(reflection, normal) <= 0.0)
    {
        return result;
    }
    // glTF thin BTDF: mirror the reflection lobe across the macrosurface, not the microfacet.
    result.direction = result.reflected ? reflection : reflect(reflection, normal);
    const float3 bsdf = EvaluateRoughThinTransmission(view, result.direction, normal,
        roughness, ior, tint, result.pdf);
    result.weight = result.pdf > 0.0 ? bsdf * abs(dot(normal, result.direction)) / result.pdf : 0.0;
    result.valid = result.pdf > 0.0 && all(isfinite(result.weight)) && all(isfinite(result.direction));
    return result;
}

#endif
