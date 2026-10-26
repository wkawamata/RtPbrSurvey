#include "../Shaders/VolumeTransmission.hlsli"
#include "../Shaders/VolumeAbsorptionParameters.hlsli"

cbuffer VolumeMaterialContract : register(b0)
{
    VolumeAbsorptionParameters g_volumeParameters;
};

struct ContractResult
{
    float4 directionAndFresnel;
    float4 eventData;
    float4 attenuation;
    float4 validation;
};
RWStructuredBuffer<ContractResult> g_results : register(u0);

[numthreads(64, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= 28 * 1024)
    {
        return;
    }
    const uint test = id.x / 1024;
    const uint pair = test / 4;
    const uint angle = test % 4;
    const float incidentIors[7] = {1, 1, 1.5, 1.5, 1, 1.33, 2};
    const float transmittedIors[7] = {1, 1.5, 1, 1.33, 2, 1, 1};
    const float cosines[4] = {1, 0.8, 0.6, 0};
    float cosine = cosines[angle];
    if (pair == 2 && (angle == 1 || angle == 2))
    {
        cosine = sqrt(1.0 - 1.0 / 2.25) + (angle == 1 ? 0.001 : -0.001);
    }
    const float3 incident = float3(sqrt(max(0.0, 1.0 - cosine * cosine)), 0, -cosine);
    const float random = (float(id.x % 1024) + 0.5) / 1024.0;
    const VolumeBoundarySample sample = SampleSmoothVolumeBoundary(incident, float3(0, 0, 1),
        incidentIors[pair], transmittedIors[pair], random);
    const float infinity = asfloat(0x7f800000u);
    const float nanValue = asfloat(0x7fc00000u);
    const float3 colors[7] = {float3(1, 1, 1), float3(0.2, 0.4, 0.8), float3(0, 0, 0),
        float3(0, 0.5, 1), float3(0.01, 0.1, 0.9), float3(0.999, 0.001, 0.5), float3(1, 1, 1)};
    const float distances[7] = {1, infinity, 2, 2, 0.5, 1, 1e-8};
    const float lengths[4] = {0, 0.5, 1, 2};
    const float3 color = pair == 3 ? g_volumeParameters.attenuationColor : colors[pair];
    const float distance = pair == 3 ? g_volumeParameters.attenuationDistance : distances[pair];
    const VolumeAttenuation attenuation = EvaluateVolumeAttenuation(color, distance, lengths[angle]);
    const VolumeBoundarySample entry = SampleSmoothVolumeBoundary(float3(0, 0, -1), float3(0, 0, 1),
        incidentIors[pair], transmittedIors[pair], 0.99);
    const VolumeBoundarySample exit = SampleSmoothVolumeBoundary(float3(0, 0, -1), float3(0, 0, 1),
        transmittedIors[pair], incidentIors[pair], 0.99);
    VolumeBoundarySample invalidBoundary;
    VolumeAttenuation invalidAttenuation;
    if (pair == 0)
    {
        invalidBoundary = SampleSmoothVolumeBoundary(incident, float3(0, 0, 1), 0, 1, random);
        invalidAttenuation = EvaluateVolumeAttenuation(1.0, 1, -1);
    }
    else if (pair == 1)
    {
        invalidBoundary = SampleSmoothVolumeBoundary(incident, float3(0, 0, 1), nanValue, 1, random);
        invalidAttenuation = EvaluateVolumeAttenuation(1.0, 0, 1);
    }
    else if (pair == 2)
    {
        invalidBoundary = SampleSmoothVolumeBoundary(incident, float3(0, 0, 1), 1, 1.5, 1);
        invalidAttenuation = EvaluateVolumeAttenuation(float3(-0.1, 1, 1), 1, 1);
    }
    else if (pair == 3)
    {
        invalidBoundary = SampleSmoothVolumeBoundary(float3(0, 0, 1), float3(0, 0, 1), 1, 1.5, random);
        invalidAttenuation = EvaluateVolumeAttenuation(float3(1.1, 1, 1), 1, 1);
    }
    else if (pair == 4)
    {
        invalidBoundary = SampleSmoothVolumeBoundary(float3(nanValue, 0, -1), float3(0, 0, 1), 1, 1.5, random);
        invalidAttenuation = EvaluateVolumeAttenuation(1.0, nanValue, 1);
    }
    else if (pair == 5)
    {
        invalidBoundary = SampleSmoothVolumeBoundary(incident, float3(0, 0, 2), 1, 1.5, random);
        invalidAttenuation = EvaluateVolumeAttenuation(1.0, 1, infinity);
    }
    else
    {
        invalidBoundary = SampleSmoothVolumeBoundary(incident, float3(0, 0, 1), 1, -1, random);
        invalidAttenuation = EvaluateVolumeAttenuation(float3(nanValue, 1, 1), 1, 1);
    }
    ContractResult result;
    result.directionAndFresnel = float4(sample.direction, sample.fresnel);
    result.eventData = float4(sample.weight, sample.eventProbability, sample.reflected ? 1 : 0, sample.valid ? 1 : 0);
    result.attenuation = float4(attenuation.transmittance, attenuation.valid ? 1 : 0);
    result.validation = float4(entry.weight * exit.weight, invalidBoundary.valid ? 1 : 0,
        invalidAttenuation.valid ? 1 : 0, sample.totalInternalReflection ? 1 : 0);
    g_results[id.x] = result;
}
