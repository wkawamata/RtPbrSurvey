#ifndef RTPBRSURVEY_DIRECT_PBR_LIGHTING_HLSLI
#define RTPBRSURVEY_DIRECT_PBR_LIGHTING_HLSLI

float3 EvaluateDirectPbrLights(PbrSurface surface, float3 viewDirection, float3 worldPosition)
{
    float3 radiance = 0.0;
    for (uint lightIndex = 0; lightIndex < lightCount; ++lightIndex)
    {
        DirectLightSample light = EvaluateDirectLight(lightIndex, worldPosition);
        if (any(light.radiance > 0.0))
        {
            radiance += EvaluatePbrDirectLighting(surface.albedo, surface.metallic,
                max(surface.roughness, 0.04), surface.normal, viewDirection,
                light.surfaceToLight, light.radiance);
        }
    }
    return radiance * directLightEnabled;
}
#endif
