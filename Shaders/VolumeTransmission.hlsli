#ifndef RTPBRSURVEY_VOLUME_TRANSMISSION_HLSLI
#define RTPBRSURVEY_VOLUME_TRANSMISSION_HLSLI

struct VolumeBoundarySample
{
    float3 direction;
    float fresnel;
    float weight;
    float eventProbability;
    bool reflected;
    bool totalInternalReflection;
    bool valid;
};

// Incident and normal are unit vectors; normal points into the incident medium.
VolumeBoundarySample SampleSmoothVolumeBoundary(float3 incident, float3 normal,
    float incidentIor, float transmittedIor, float random)
{
    VolumeBoundarySample result = (VolumeBoundarySample)0;
    if (!all(isfinite(incident)) || !all(isfinite(normal)) ||
        abs(dot(incident, incident) - 1.0) > 0.0002 || abs(dot(normal, normal) - 1.0) > 0.0002 ||
        !isfinite(incidentIor) || !isfinite(transmittedIor) || incidentIor <= 0.0 || transmittedIor <= 0.0 ||
        !isfinite(random) || random < 0.0 || random >= 1.0 || dot(incident, normal) > 0.0)
    {
        return result;
    }
    const float cosineI = saturate(-dot(incident, normal));
    const float eta = incidentIor / transmittedIor;
    const float etaSquared = eta * eta;
    if (!isfinite(etaSquared) || etaSquared <= 0.0)
    {
        return result;
    }
    const float sineSquaredT = etaSquared * (1.0 - cosineI) * (1.0 + cosineI);
    const float cosineT = sqrt(max(0.0, 1.0 - sineSquaredT));
    result.totalInternalReflection = incidentIor != transmittedIor && sineSquaredT >= 1.0;
    if (incidentIor == transmittedIor)
    {
        result.fresnel = 0.0;
    }
    else if (result.totalInternalReflection)
    {
        result.fresnel = 1.0;
    }
    else
    {
        const float relativeIor = transmittedIor / incidentIor;
        const float parallel = (relativeIor * cosineI - cosineT) / (relativeIor * cosineI + cosineT);
        const float perpendicular = (cosineI - relativeIor * cosineT) / (cosineI + relativeIor * cosineT);
        result.fresnel = saturate(0.5 * (parallel * parallel + perpendicular * perpendicular));
    }
    result.reflected = random < result.fresnel;
    result.eventProbability = result.reflected ? result.fresnel : 1.0 - result.fresnel;
    result.direction = result.reflected ? reflect(incident, normal) :
        (incidentIor == transmittedIor ? incident : eta * incident + (eta * cosineI - cosineT) * normal);
    // f * abs(cos) / discrete event probability, for radiance transport.
    result.weight = result.reflected ? 1.0 : etaSquared;
    result.valid = result.eventProbability > 0.0 && all(isfinite(result.direction)) && isfinite(result.weight);
    return result;
}

struct VolumeAttenuation
{
    float3 transmittance;
    bool valid;
};

float VolumeAttenuationChannel(float color, float relativeDistance)
{
    if (color == 0.0)
    {
        return 0.0;
    }
    if (color == 1.0)
    {
        return 1.0;
    }
    return exp(log(color) * relativeDistance);
}

VolumeAttenuation EvaluateVolumeAttenuation(float3 color, float attenuationDistance, float length)
{
    VolumeAttenuation result = (VolumeAttenuation)0;
    if (!all(isfinite(color)) || any(color < 0.0) || any(color > 1.0) ||
        isnan(attenuationDistance) || attenuationDistance <= 0.0 || !isfinite(length) || length < 0.0)
    {
        return result;
    }
    result.valid = true;
    if (length == 0.0 || isinf(attenuationDistance))
    {
        result.transmittance = 1.0;
        return result;
    }
    const float relativeDistance = length / attenuationDistance;
    result.transmittance = float3(VolumeAttenuationChannel(color.r, relativeDistance),
        VolumeAttenuationChannel(color.g, relativeDistance), VolumeAttenuationChannel(color.b, relativeDistance));
    return result;
}

#endif
