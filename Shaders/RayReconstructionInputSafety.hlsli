#ifndef RTPBRSURVEY_RR_INPUT_SAFETY
#define RTPBRSURVEY_RR_INPUT_SAFETY

#include "RayReconstructionInputConversion.hlsli"

// Experimental application envelope, not an SDK format/range guarantee.
static const uint kRrNumericEnvelopeRejected = 7;

RrConvertedInputs RrSafePlaceholder(uint status)
{
    RrConvertedInputs result = (RrConvertedInputs)0;
    result.color.w = result.diffuseAlbedo.w = result.specularAlbedo.w = 1;
    result.normalRoughness = float4(0, 0, 1, 1);
    result.depthMotionHit.x = 1;
    result.diagnostics.x = status;
    return result;
}

RrConvertedInputs SanitizeRrInputs(RrConvertedInputs value, bool admittedBackground)
{
    if (value.diagnostics.x == 0 && admittedBackground)
        value.normalRoughness.xyz = float3(0, 0, 1);
    const bool valid = all(isfinite(value.color)) && all(value.color.xyz >= 0) && all(value.color.xyz <= 65504) &&
        value.color.w == 1 && all(isfinite(value.diffuseAlbedo)) && all(value.diffuseAlbedo.xyz >= 0) &&
        all(value.diffuseAlbedo.xyz <= 1) && value.diffuseAlbedo.w == 1 && all(isfinite(value.specularAlbedo)) &&
        all(value.specularAlbedo.xyz >= 0) && all(value.specularAlbedo.xyz <= 65504) && value.specularAlbedo.w == 1 &&
        all(isfinite(value.normalRoughness)) && abs(length(value.normalRoughness.xyz) - 1) <= .002 &&
        value.normalRoughness.w >= 0 && value.normalRoughness.w <= 1 && all(isfinite(value.depthMotionHit)) &&
        value.depthMotionHit.x > 0 && value.depthMotionHit.x <= 65504 && all(abs(value.depthMotionHit.yz) <= 1) &&
        value.depthMotionHit.w >= 0 && value.depthMotionHit.w <= 65504;
    if (value.diagnostics.x != 0 || !valid)
        return RrSafePlaceholder(value.diagnostics.x != 0 ? (uint)value.diagnostics.x : kRrNumericEnvelopeRejected);
    return value;
}

#endif
