#ifndef RTPBRSURVEY_NRD_RELAX_COMPOSITION
#define RTPBRSURVEY_NRD_RELAX_COMPOSITION

struct NrdCompositionResult
{
    float4 color;
    // x: NRD used, y: reason (0 used, 1 excluded, 2 mask, 3 factors,
    // 4 radiance, 5 overflow, 6 invalid Native), zw: inactive lobes.
    float4 diagnostics;
};

NrdCompositionResult ComposeNrdRadiance(float3 diffuse, float3 specular,
    float4 diffuseFactor, float4 specularFactor, float3 stable, float3 background,
    float4 nativeColor, float4 admission)
{
    NrdCompositionResult result = (NrdCompositionResult)0;
    result.color = float4(0, 0, 0, 1);
    result.diagnostics.y = 6;
    if (any(!isfinite(nativeColor)) || any(nativeColor.rgb < 0) || nativeColor.w != 1)
        return result;
    result.color = nativeColor;
    result.diagnostics.y = 2;
    if (any(!isfinite(admission)) || any((admission.xzw != 0) & (admission.xzw != 1)) ||
        admission.y < 0 || admission.y > 4 || admission.y != floor(admission.y) ||
        (admission.x == 1 && admission.y != 0) || (admission.x == 0 && any(admission.zw != 0)))
        return result;
    result.diagnostics.y = 1;
    if (admission.x == 0)
        return result;
    result.diagnostics.y = 3;
    if (any(!isfinite(diffuseFactor)) || any(!isfinite(specularFactor)) ||
        diffuseFactor.w != 1 || specularFactor.w != 1 || any(diffuseFactor.rgb <= 0) ||
        any(specularFactor.rgb <= 0) || any(diffuseFactor.rgb > 1) || any(specularFactor.rgb > 1))
        return result;
    result.diagnostics.y = 4;
    if (any(!isfinite(diffuse)) || any(!isfinite(specular)) || any(!isfinite(stable)) ||
        any(!isfinite(background)) || any(diffuse < 0) || any(specular < 0) ||
        any(stable < 0) || any(background < 0))
        return result;
    float3 restoredDiffuse = admission.z == 1 ? 0 : diffuse * diffuseFactor.rgb;
    float3 restoredSpecular = admission.w == 1 ? 0 : specular * specularFactor.rgb;
    float3 color = ((restoredDiffuse + restoredSpecular) + stable) + background;
    result.diagnostics.y = 5;
    if (any(!isfinite(color)) || any(color < 0))
        return result;
    result.color = float4(color, 1);
    result.diagnostics = float4(1, 0, admission.zw);
    return result;
}
#endif
