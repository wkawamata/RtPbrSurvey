#include "stdafx.h"

#include "NrdRelaxAdapter.h"

namespace Engine
{

NrdRelaxSupportInfo QueryNrdRelaxSupport()
{
    NrdRelaxSupportInfo info;
    info.targetSdkVersion = "4.17.3";
    info.reason = "NRD RELAX dispatch and PT input conversion are not integrated.";
    return info;
}

NrdRelaxSamplingReadiness QueryNrdRelaxSamplingReadiness(unsigned int samplesPerFrame,
                                                       bool pixelCenterPrimary)
{
    if (samplesPerFrame != 1)
    {
        return {false, "RELAX prototype requires exactly one primary sample per pixel/frame."};
    }
    if (!pixelCenterPrimary)
    {
        return {false, "RELAX prototype requires pixel-center primary sampling."};
    }
    return {true, "Sampling only is coherent; packing, distances and dispatch remain unvalidated."};
}

}
