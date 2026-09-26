#ifndef RTPBRSURVEY_DIRECT_LIGHTS_HLSLI
#define RTPBRSURVEY_DIRECT_LIGHTS_HLSLI

#include "DirectLightEvaluation.hlsli"

// Layout shared with RtPbrSurveyEngine::LightingConstants. Legacy prefix retained
// for passes reading only the environment/debug settings.
cbuffer LightingConstants : register(b2)
{
    float3 lightDirection;
    float iblIntensity;
    float3 lightColor;
    float diffuseIntensity;
    float4 backgroundColor;
    float skyboxEnabled;
    float skyboxPreview;
    float skyboxPreviewExposure;
    float lightPassDebugViewMode;
    float directLightEnabled;
    float diffuseIblEnabled;
    float specularIblEnabled;
    float emissiveEnabled;
    float iblDebugMip;
    float iblDebugExposure;
    float rayTracingSupported;
    float shadowMaskBlurEnabled;
    float reflectionHitOverlayEnabled;
    float reflectionHitOverlayIntensity;
    float reflectionHitOverlayMode;
    float reflectionContributionEnabled;
    float reflectionContributionIntensity;
    float reflectionContributionMaxDistance;
    uint lightCount;
    uint primaryShadowLightIndex;
    LightGpuData lights[16];
    float reflectionRayNormalBias;
    uint reflectionLightSamplingEnabled;
    uint reflectionLightSamplingFrame;
};

DirectLightSample EvaluateDirectLight(uint index, float3 worldPosition)
{
    return EvaluateDirectLightData(lights[index], worldPosition);
}
#endif
