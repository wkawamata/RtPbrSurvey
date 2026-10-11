#ifndef RTPBRSURVEY_PATH_TRACING_DENOISER_SAMPLING_HLSLI
#define RTPBRSURVEY_PATH_TRACING_DENOISER_SAMPLING_HLSLI

static const float kDenoiserSamplingPi = 3.14159265359;

bool ValidDenoiserRandomSample(float2 sample)
{
    return all(isfinite(sample)) && all(sample >= 0.0) && all(sample < 1.0);
}

// Heitz 2018, JCGT 7(4):1, corrected 2019. Local frame has normal +Z.
float3 SampleDenoiserGgxVisibleNormal(float2 sample, float roughness, float3 view)
{
    if (!ValidDenoiserRandomSample(sample) || !isfinite(roughness) || roughness < 0.0 || roughness > 1.0 ||
        !all(isfinite(view)) || abs(dot(view, view) - 1.0) > 0.0001 || view.z <= 0.0)
    {
        return 0.0;
    }
    const float alpha = max(roughness * roughness, 0.001);
    const float3 stretchedView = normalize(float3(alpha * view.xy, view.z));
    const float projectedLengthSquared = dot(stretchedView.xy, stretchedView.xy);
    const float3 tangent = projectedLengthSquared > 0.0
                               ? float3(-stretchedView.y, stretchedView.x, 0.0) * rsqrt(projectedLengthSquared)
                               : float3(1.0, 0.0, 0.0);
    const float3 bitangent = cross(stretchedView, tangent);
    const float radius = sqrt(sample.x);
    const float angle = 2.0 * kDenoiserSamplingPi * sample.y;
    const float diskX = radius * cos(angle);
    const float blend = 0.5 * (1.0 + stretchedView.z);
    const float diskYBase = radius * sin(angle);
    precise float arc = sqrt(max(0.0, (1.0 - sample.x) + diskYBase * diskYBase));
    const float diskY = lerp(arc, diskYBase, blend);
    // Factor the residual to avoid cancellation near the disk boundary.
    const float arcMinusBase = arc - diskYBase;
    precise float projectedHeight =
        sqrt(max(0.0, blend * ((1.0 - sample.x) + (1.0 - blend) * arcMinusBase * arcMinusBase)));
    const float3 hemisphereNormal = diskX * tangent + diskY * bitangent + projectedHeight * stretchedView;
    return normalize(float3(alpha * hemisphereNormal.xy, max(0.0, hemisphereNormal.z)));
}

float DenoiserGgxVisibleNormalPdf(float3 halfVector, float roughness, float3 view)
{
    if (!all(isfinite(halfVector)) || halfVector.z <= 0.0 || dot(view, halfVector) <= 0.0 || view.z <= 0.0 ||
        !isfinite(roughness) || roughness < 0.0 || roughness > 1.0 || !all(isfinite(view)) ||
        abs(dot(view, view) - 1.0) > 0.0001 || abs(dot(halfVector, halfVector) - 1.0) > 0.0001)
    {
        return 0.0;
    }
    const float alpha = max(roughness * roughness, 0.001);
    const float alphaSquared = alpha * alpha;
    const float denominator = dot(halfVector.xy, halfVector.xy) + alphaSquared * halfVector.z * halfVector.z;
    const float distribution = alphaSquared / (kDenoiserSamplingPi * denominator * denominator);
    const float maskingRatio = 2.0 / (view.z + sqrt(alphaSquared + (1.0 - alphaSquared) * view.z * view.z));
    return distribution * maskingRatio * dot(view, halfVector);
}

float3 SampleDenoiserCosineDirection(float2 sample)
{
    if (!ValidDenoiserRandomSample(sample))
    {
        return 0.0;
    }
    const float radius = sqrt(sample.x);
    const float angle = 2.0 * kDenoiserSamplingPi * sample.y;
    return float3(radius * cos(angle), radius * sin(angle), sqrt(1.0 - sample.x));
}

#endif
