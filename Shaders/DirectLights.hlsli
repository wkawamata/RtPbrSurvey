#ifndef RTPBRSURVEY_DIRECT_LIGHTS_HLSLI
#define RTPBRSURVEY_DIRECT_LIGHTS_HLSLI

struct LightGpuData
{
    float3 position;
    float range;
    float3 direction;
    uint type;
    float3 radiance;
    float innerCos;
    float outerCos;
    uint enabled;
    uint2 padding;
};

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

struct DirectLightSample
{
    float3 surfaceToLight;
    float3 radiance;
    float distance;
};

DirectLightSample EvaluateDirectLight(uint index, float3 worldPosition)
{
    const LightGpuData light = lights[index];
    DirectLightSample result;
    result.surfaceToLight = -light.direction;
    result.radiance = light.radiance * light.enabled;
    result.distance = 1e30;
    if (light.type != 0u)
    {
        const float3 offset = light.position - worldPosition;
        const float distanceSquared = dot(offset, offset);
        result.distance = sqrt(distanceSquared);
        // At the source itself direction is undefined: return zero instead of NaN.
        result.surfaceToLight = distanceSquared > 1e-12 ? offset / result.distance : -light.direction;
        const float ratio = result.distance / light.range;
        const float window = saturate(1.0 - ratio * ratio * ratio * ratio);
        // Relative radiance at distance 1; minimum squared distance is 1 cm squared.
        result.radiance *= window * window / max(distanceSquared, 0.0001);
        if (distanceSquared <= 1e-12)
        {
            result.radiance = 0.0;
        }
        if (light.type == 2u)
        {
            // Distinct small angles can round to identical float cosines.
            const float cone = saturate((dot(light.direction, -result.surfaceToLight) - light.outerCos) /
                max(light.innerCos - light.outerCos, 0.000001));
            result.radiance *= cone * cone * (3.0 - 2.0 * cone);
        }
    }
    return result;
}
#endif
