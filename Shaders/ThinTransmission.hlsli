#ifndef RTPBRSURVEY_THIN_TRANSMISSION_HLSLI
#define RTPBRSURVEY_THIN_TRANSMISSION_HLSLI

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
};

ThinTransmissionSample SampleThinTransmission(float3 incident, float3 normal, float ior, float3 tint, float random)
{
    const float fresnel = ThinTransmissionFresnel(dot(-incident, normal), ior);
    ThinTransmissionSample result;
    result.reflected = random < fresnel;
    result.direction = result.reflected ? reflect(incident, normal) : incident;
    result.weight = result.reflected ? float3(1.0, 1.0, 1.0) : tint;
    return result;
}

#endif
