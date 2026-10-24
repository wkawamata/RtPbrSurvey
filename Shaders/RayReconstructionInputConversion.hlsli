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

struct RrConvertedInputs
{
    float4 color;
    float4 diffuseAlbedo;
    float4 specularAlbedo;
    float4 normalRoughness;
    float4 depthMotionHit; // LinearDepth, top-down UV motion XY, world-space distance.
    float4 diagnostics;    // x: status; zero means numeric candidate, NOT SDK-ready.
};

// Only finite traced-hit distance candidates are supported. Background, Unlit
// (untraced), miss, invalid and inactive states remain whole-frame fallback cases.
// Even a candidate requires separate ray/radiance correlation qualification.
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
    if (any(!isfinite(specularDistance)) || specularDistance.y != 1 || specularDistance.x <= 0 ||
        specularDistance.z <= 0 || specularDistance.w != 0)
        return result;
    float3 normal = normalize(normalRoughness.xyz);
    float3 specular =
        RrSpecularAlbedo(f0Metallic.xyz, normalRoughness.w * normalRoughness.w, dot(normal, normalize(view)));
    result.diagnostics.x = kRrInvalidMaterial;
    if (any(!isfinite(specular)) || any(specular < 0))
        return result;
    // No accumulation, demodulation, half-float miss sentinel or composition.
    result.color = float4(frameRadiance, 1);
    result.diffuseAlbedo = float4(baseColor.xyz * (1 - f0Metallic.w), 1);
    result.specularAlbedo = float4(specular, 1);
    result.normalRoughness = float4(normal, normalRoughness.w);
    result.depthMotionHit = float4(viewZ, motionNdc * float2(.5, -.5), specularDistance.x);
    result.diagnostics.x = kRrConversionCandidate;
    return result;
}
#endif
