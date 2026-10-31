#include "../Shaders/RoughVolumeTransmission.hlsli"

struct ContractResult
{
    float4 directionAndPdf;
    float4 weightAndEvent;
    float4 evaluation;
    float4 halfVectorAndInvalid;
};
RWStructuredBuffer<ContractResult> g_results : register(u0);

[numthreads(64, 1, 1)] void CSMain(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= 28 * 1024)
    {
        return;
    }
    const uint test = id.x / 1024;
    const uint index = id.x % 1024;
    const float roughnesses[7] = {0, .1, .35, .7, 1, .35, .7};
    const float cosines[4] = {1, .8, .6, .1};
    const float roughness = roughnesses[test / 4];
    const float incidentIor = test / 4 >= 5 ? 1.5 : 1;
    const float transmittedIor = test / 4 == 5 ? 1 : 1.5;
    const float z = cosines[test % 4];
    const float3 view = float3(sqrt(1 - z * z), 0, z);
    const float2 directionSample = (float2(index % 32, index / 32) + .5) / 32;
    const float eventSample = (float((index * 73) % 1024) + .5) / 1024;
    const RoughVolumeBoundarySample sample = SampleRoughVolumeBoundary(-view, roughness,
        incidentIor, transmittedIor, eventSample, directionSample);
    float evaluatedPdf;
    const float evaluated = EvaluateRoughVolumeBoundary(view, sample.direction, roughness,
        incidentIor, transmittedIor, evaluatedPdf);
    const float3 halfVector = roughness == 0 || incidentIor == transmittedIor ? float3(0, 0, 1) :
        SampleDenoiserGgxVisibleNormal(directionSample, roughness, view);
    RoughVolumeBoundarySample invalid = SampleRoughVolumeBoundary(-view, roughness,
        incidentIor, transmittedIor, 1, directionSample);
    bool invalidAccepted = invalid.valid || invalid.weight != 0 || invalid.pdf != 0 || any(invalid.direction != 0);
    const float nanValue = asfloat(0x7fc00000u);
    switch (test / 4)
    {
    case 0:
        invalid = SampleRoughVolumeBoundary(-view, -0.1, incidentIor, transmittedIor, eventSample, directionSample);
        break;
    case 1:
        invalid = SampleRoughVolumeBoundary(-view * 2, roughness, incidentIor, transmittedIor, eventSample, directionSample);
        break;
    case 2:
        invalid = SampleRoughVolumeBoundary(-view, roughness, 0, transmittedIor, eventSample, directionSample);
        break;
    case 3:
        invalid = SampleRoughVolumeBoundary(-view, roughness, incidentIor, nanValue, eventSample, directionSample);
        break;
    case 4:
        invalid = SampleRoughVolumeBoundary(-view, roughness, incidentIor, transmittedIor, eventSample, float2(1, 0));
        break;
    case 5:
        invalid = SampleRoughVolumeBoundary(-view, roughness, incidentIor, transmittedIor, nanValue, directionSample);
        break;
    default:
        invalid = SampleRoughVolumeBoundary(-view, 2, incidentIor, transmittedIor, eventSample, directionSample);
        break;
    }
    invalidAccepted = invalidAccepted || invalid.valid || invalid.weight != 0 || invalid.pdf != 0 || any(invalid.direction != 0);
    float invalidPdf;
    const float invalidValue = EvaluateRoughVolumeBoundary(view, float3(0, 0, 0), roughness,
        incidentIor, transmittedIor, invalidPdf);
    ContractResult result;
    result.directionAndPdf = float4(sample.direction, sample.pdf);
    result.weightAndEvent = float4(sample.weight, sample.fresnel, sample.reflected, sample.valid);
    result.evaluation = float4(evaluated, evaluatedPdf, sample.delta, sample.eventProbability);
    result.halfVectorAndInvalid = float4(halfVector, invalidAccepted || invalidPdf != 0 || invalidValue != 0);
    g_results[id.x] = result;
}
