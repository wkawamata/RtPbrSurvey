#ifndef RTPBRSURVEY_NRD_RELAX_INPUT_CONVERSION
#define RTPBRSURVEY_NRD_RELAX_INPUT_CONVERSION

#include "NRD.hlsli"

static const uint kNrdSignalPackable = 0;
static const uint kNrdSignalInvalid = 1;
static const uint kNrdSignalOverflow = 2;
static const uint kNrdSignalInactive = 3;

struct NrdRelaxConvertedSignals
{
    float4 diffuse;
    float4 specular;
    float3 diffuseFactor;
    float3 specularFactor;
    uint diffuseStatus;
    uint specularStatus;
    bool conversionReady;
};

uint InspectNrdGpuSignal(float3 physical, float3 factor, float distance, bool active)
{
    if (any(!isfinite(physical)) || any(physical < 0) || any(!isfinite(factor)) || any(factor <= 0) ||
        any(factor > 1) || !isfinite(distance) || distance < 0 || distance > 65504 ||
        (!active && (any(physical != 0) || distance != 0)))
    {
        return kNrdSignalInvalid;
    }
    if (!active)
    {
        return kNrdSignalInactive;
    }
    const float3 demodulated = physical / factor;
    if (any(!isfinite(demodulated)) || any(demodulated > 65504))
    {
        return kNrdSignalOverflow;
    }
    return kNrdSignalPackable;
}

// Distances must already satisfy the ray-state and geometry range contracts.
NrdRelaxConvertedSignals ConvertNrdRelaxSignals(float3 normal,
                                                float3 view,
                                                float3 baseColor,
                                                float metallic,
                                                float3 f0,
                                                float roughness,
                                                float3 physicalDiffuse,
                                                float3 physicalSpecular,
                                                float diffuseDistance,
                                                float specularDistance)
{
    NrdRelaxConvertedSignals result = (NrdRelaxConvertedSignals)0;
    result.diffuseStatus = kNrdSignalInvalid;
    result.specularStatus = kNrdSignalInvalid;
    if (any(!isfinite(normal)) || any(!isfinite(view)) || any(!isfinite(baseColor)) || any(!isfinite(f0)) ||
        !isfinite(metallic) || !isfinite(roughness) || any(baseColor < 0) || any(baseColor > 1) || any(f0 < 0) ||
        any(f0 > 1) || metallic < 0 || metallic > 1 || roughness < 0 || roughness > 1 ||
        abs(length(normal) - 1) > .005 || abs(length(view) - 1) > .005)
    {
        return result;
    }
    const bool diffuseActive = metallic < 1 && any(baseColor > 0);
    const bool specularActive = true;
    NRD_MaterialFactors(normalize(normal),
                        normalize(view),
                        baseColor * (1 - metallic),
                        f0,
                        roughness,
                        result.diffuseFactor,
                        result.specularFactor);
    result.diffuseStatus = InspectNrdGpuSignal(physicalDiffuse, result.diffuseFactor, diffuseDistance, diffuseActive);
    result.specularStatus =
        InspectNrdGpuSignal(physicalSpecular, result.specularFactor, specularDistance, specularActive);
    result.conversionReady =
        (result.diffuseStatus == kNrdSignalPackable || result.diffuseStatus == kNrdSignalInactive) &&
        (result.specularStatus == kNrdSignalPackable || result.specularStatus == kNrdSignalInactive);
    if (!result.conversionReady)
    {
        return result;
    }
    if (diffuseActive)
    {
        result.diffuse =
            RELAX_FrontEnd_PackRadianceAndHitDist(physicalDiffuse / result.diffuseFactor, diffuseDistance, true);
    }
    if (specularActive)
    {
        result.specular =
            RELAX_FrontEnd_PackRadianceAndHitDist(physicalSpecular / result.specularFactor, specularDistance, true);
    }
    return result;
}
#endif
