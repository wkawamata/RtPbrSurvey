#pragma once

namespace Engine
{

enum class NrdRelaxSupportStatus
{
    NotIntegrated = 0,
};

struct NrdRelaxSupportInfo
{
    bool available = false;
    NrdRelaxSupportStatus status = NrdRelaxSupportStatus::NotIntegrated;
    const char* targetSdkVersion = nullptr;
    const char* reason = nullptr;
};

NrdRelaxSupportInfo QueryNrdRelaxSupport();

struct NrdRelaxSamplingReadiness
{
    bool ready = false;
    const char* reason = nullptr;
};

NrdRelaxSamplingReadiness QueryNrdRelaxSamplingReadiness(unsigned int samplesPerFrame,
                                                       bool pixelCenterPrimary);

}
