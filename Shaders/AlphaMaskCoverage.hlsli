#ifndef RTPBRSURVEY_ALPHA_MASK_COVERAGE_HLSLI
#define RTPBRSURVEY_ALPHA_MASK_COVERAGE_HLSLI

bool PassAlphaMaskCoverage(float textureAlpha, float factorAlpha, float cutoff)
{
    return textureAlpha * factorAlpha >= cutoff;
}

#endif
