#ifndef RTPBRSURVEY_RR_INPUT_CONVERSION
#define RTPBRSURVEY_RR_INPUT_CONVERSION

// Streamline v2.12.0 ProgrammingGuideDLSS_RR.md, EnvBRDFApprox2.
// This is view-dependent reflectance, not the NRD demodulation factor.
float3 RrSpecularAlbedo(float3 f0, float alpha, float noV)
{
    noV = abs(noV);
    float4 x = float4(1, noV, noV * noV, noV * noV * noV);
    float4 y = float4(1, alpha, alpha * alpha, alpha * alpha * alpha);
    const float2x2 m1 = float2x2(.99044, -1.28514, 1.29678, -.755907);
    const float3x3 m2 = float3x3(1, 2.92338, 59.4188, 20.3225, -27.0302, 222.592, 121.563, 626.13, 316.627);
    const float2x2 m3 = float2x2(.0365463, 3.32707, 9.0632, -9.04756);
    const float3x3 m4 = float3x3(1, 3.59685, -1.36772, 9.04401, -16.3174, 9.22949, 5.56589, 19.7886, -20.2123);
    float bias = dot(mul(m1, x.xy), y.xy) / dot(mul(m2, x.xyw), y.xyw);
    float scale = dot(mul(m3, x.xy), y.xy) / dot(mul(m4, x.xzw), y.xyw);
    bias *= saturate(f0.g * 50);
    return f0 * max(0, scale) + max(0, bias);
}

static const uint kRrConversionCandidate = 0;
static const uint kRrUnsupportedPrimary = 1;
static const uint kRrInvalidMaterial = 2;
static const uint kRrInvalidGeometry = 3;
static const uint kRrInvalidRadiance = 4;
static const uint kRrUnsupportedDistance = 5;

// RR-specific environment distance, following NVIDIA's vk_gltf_renderer
// shaders/gltf_pathtrace.slang specular-guide miss path. This is not an NRD
// packed distance and must never be substituted for an untraced sample.
static const float kRrSpecularEnvironmentDistance = 65504.0;

float4 RrActualSpecularDistance(float4 ray, float4 surface, float metallic, float roughness)
{
    float4 distance = float4(surface.x, ray.z, ray.x, 0);
    if (any(!isfinite(ray)) || any(!isfinite(surface)) || ray.y != 2 || metallic != 1 || roughness != 0 || ray.w != 0 ||
        any(surface.zw != 0) || surface.y != ray.z)
        distance.y = 0;
    return distance;
}

struct RrConvertedInputs
{
    float4 color;
    float4 diffuseAlbedo;
    float4 specularAlbedo;
    float4 normalRoughness;
    float4 depthMotionHit; // LinearDepth, top-down UV motion XY, world-space distance.
    float4 diagnostics;    // x: status; zero means numeric candidate, NOT SDK-ready.
};

// Traced miss is admitted only by the actual-continuation live caller. Legacy
// independent-probe conversion retains its finite-hit-only contract. Primary
// background, untraced and invalid states remain whole-frame fallback cases.
RrConvertedInputs ConvertRrInputs(float3 frameRadiance,
                                  float4 normalRoughness,
                                  float viewZ,
                                  float2 motionNdc,
                                  float4 baseColor,
                                  float4 f0Metallic,
                                  float4 primaryPosition,
                                  float4 specularDistance,
                                  float3 view,
                                  bool allowTracedSpecularMiss,
                                  bool allowAbsentSpecularDistance = false)
{
    RrConvertedInputs result = (RrConvertedInputs)0;
    result.diagnostics.x = kRrUnsupportedPrimary;
    if (any(!isfinite(primaryPosition)) || primaryPosition.w != 1 || baseColor.w != 1)
        return result;
    result.diagnostics.x = kRrInvalidMaterial;
    if (any(!isfinite(baseColor)) || any(!isfinite(f0Metallic)) || any(baseColor.xyz < 0) || any(baseColor.xyz > 1) ||
        any(f0Metallic < 0) || any(f0Metallic > 1) || !isfinite(normalRoughness.w) || normalRoughness.w < 0 ||
        normalRoughness.w > 1)
        return result;
    result.diagnostics.x = kRrInvalidGeometry;
    if (any(!isfinite(normalRoughness.xyz)) || any(!isfinite(view)) || abs(length(normalRoughness.xyz) - 1) > .002 ||
        abs(length(view) - 1) > .002 || !isfinite(viewZ) || viewZ <= 0 || any(!isfinite(motionNdc)))
        return result;
    result.diagnostics.x = kRrInvalidRadiance;
    if (any(!isfinite(frameRadiance)) || any(frameRadiance < 0))
        return result;
    result.diagnostics.x = kRrUnsupportedDistance;
    const bool absent = allowAbsentSpecularDistance && all(specularDistance == 0);
    const bool tracedHit = specularDistance.y == 1 && specularDistance.x > 0;
    const bool tracedMiss = allowTracedSpecularMiss && specularDistance.y == 2 && specularDistance.x == 0;
    if (any(!isfinite(specularDistance)) || (!tracedHit && !tracedMiss && !absent) ||
        (!absent && specularDistance.z <= 0) || specularDistance.w != 0)
        return result;
    float3 normal = normalize(normalRoughness.xyz);
    float3 specular =
        RrSpecularAlbedo(f0Metallic.xyz, normalRoughness.w * normalRoughness.w, dot(normal, normalize(view)));
    result.diagnostics.x = kRrInvalidMaterial;
    if (any(!isfinite(specular)) || any(specular < 0))
        return result;
    // No accumulation, demodulation or composition. Miss is an explicit RR
    // guide convention; a finite hit, even above 65504, is never clamped to it.
    result.color = float4(frameRadiance, 1);
    result.diffuseAlbedo = float4(baseColor.xyz * (1 - f0Metallic.w), 1);
    result.specularAlbedo = float4(specular, 1);
    result.normalRoughness = float4(normal, normalRoughness.w);
    result.depthMotionHit =
        float4(viewZ, motionNdc * float2(.5, -.5), tracedMiss ? kRrSpecularEnvironmentDistance : specularDistance.x);
    result.diagnostics.x = kRrConversionCandidate;
    return result;
}

RrConvertedInputs ConvertRrInputs(float3 frameRadiance,
                                  float4 normalRoughness,
                                  float viewZ,
                                  float2 motionNdc,
                                  float4 baseColor,
                                  float4 f0Metallic,
                                  float4 primaryPosition,
                                  float4 specularDistance,
                                  float3 view)
{
    return ConvertRrInputs(frameRadiance,
                           normalRoughness,
                           viewZ,
                           motionNdc,
                           baseColor,
                           f0Metallic,
                           primaryPosition,
                           specularDistance,
                           view,
                           false);
}

// A missing specular sample is zero, never an environment hit. Diffuse rays
// still validate their own query state before their specular guide becomes zero.
float4 RrLiveSpecularDistance(float4 ray, float4 surface)
{
    const float4 invalid = float4(0, -1, 0, 0);
    if (any(!isfinite(ray)) || any(!isfinite(surface)))
        return invalid;
    if (all(ray == 0) && all(surface == 0))
        return 0;
    if ((ray.y != 1 && ray.y != 2) || ray.x <= 0 || ray.w != 0 || any(surface.zw != 0) || surface.y != ray.z ||
        !((ray.z == 1 && surface.x > 0) || (ray.z == 2 && surface.x == 0)))
        return invalid;
    return ray.y == 2 ? float4(surface.x, ray.z, ray.x, 0) : float4(0, 0, 0, 0);
}

RrConvertedInputs ConvertRrLiveInputs(float3 radiance,
                                      float4 normal,
                                      float depth,
                                      float2 motion,
                                      float4 base,
                                      float4 material,
                                      float4 primary,
                                      float4 ray,
                                      float4 surface,
                                      float3 view)
{
    if (all(primary == 0) && all(base == 0) && all(material == 0) && all(normal == float4(0, 0, 0, 1)) && depth == 0 &&
        all(ray == 0) && all(surface == 0))
    {
        RrConvertedInputs result = (RrConvertedInputs)0;
        result.diagnostics.x = kRrInvalidGeometry;
        if (any(!isfinite(motion)))
            return result;
        result.diagnostics.x = kRrInvalidRadiance;
        if (any(!isfinite(radiance)) || any(radiance < 0))
            return result;
        // Background is a direction at infinity, not a fabricated primary hit.
        // Finite linear-depth sentinel keeps the SDK inputs numeric.
        result.color = float4(radiance, 1);
        result.diffuseAlbedo = float4(saturate(radiance), 1);
        result.specularAlbedo = float4(0, 0, 0, 1);
        result.normalRoughness = float4(0, 0, 0, 1);
        result.depthMotionHit = float4(65504, motion * float2(.5, -.5), 0);
        result.diagnostics.x = kRrConversionCandidate;
        return result;
    }
    return ConvertRrInputs(radiance,
                           normal,
                           depth,
                           motion,
                           base,
                           material,
                           primary,
                           RrLiveSpecularDistance(ray, surface),
                           view,
                           true,
                           true);
}
#endif
