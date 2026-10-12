#include "Renderer/NrdRelaxAdapter.h"

#include <cstring>
#include <iostream>
#include <limits>

int main()
{
    const Engine::NrdRelaxSupportInfo defaults;
    const Engine::NrdRelaxSupportInfo info = Engine::QueryNrdRelaxSupport();
    bool passed = !defaults.available && !info.available &&
                  defaults.status == Engine::NrdRelaxSupportStatus::NotIntegrated &&
                  info.status == Engine::NrdRelaxSupportStatus::NotIntegrated && info.targetSdkVersion != nullptr &&
                  std::strcmp(info.targetSdkVersion, "4.17.3") == 0 && info.reason != nullptr && info.reason[0] != '\0';
    passed &= !Engine::QueryNrdRelaxSamplingReadiness(0, true).ready;
    passed &= !Engine::QueryNrdRelaxSamplingReadiness(4, true).ready;
    passed &= !Engine::QueryNrdRelaxSamplingReadiness(1, false).ready;
    passed &= Engine::QueryNrdRelaxSamplingReadiness(1, true).ready;
    passed &= !Engine::QueryNrdRelaxSupport().available;
    using Disposition = Engine::NrdRelaxDistanceDisposition;
    const auto inspect = [](float distance, float state, float raw, bool primary = true, bool active = true)
    { return Engine::InspectNrdRelaxDistance(distance, state, raw, 0.0f, 100.0f, primary, active); };
    passed &= inspect(2.0f, 1.0f, 1.999f).disposition == Disposition::HitCandidate;
    passed &= inspect(2.0f, 1.0f, 1.999f).hitDistance == 2.0f;
    passed &= inspect(0.0f, 1.0f, 1.0f).disposition == Disposition::InvalidData;
    passed &= inspect(2.0f, 1.0f, 101.0f).disposition == Disposition::InvalidData;
    passed &= inspect(65505.0f, 1.0f, 50.0f).disposition == Disposition::PackingRangeExceeded;
    passed &= inspect(0.0f, 2.0f, 100.0f).disposition == Disposition::EnvironmentMissPolicyRequired;
    passed &= inspect(0.0f, 2.0f, 100.0f).hitDistance == 0.0f;
    passed &= inspect(0.0f, 2.0f, 99.0f).disposition == Disposition::InvalidData;
    passed &= inspect(1.0f, 2.0f, 100.0f).disposition == Disposition::InvalidData;
    passed &= inspect(0.0f, 3.0f, 0.0f).disposition == Disposition::ReplacementRayRequired;
    passed &= inspect(0.0f, 0.0f, 0.0f).disposition == Disposition::TraceRequired;
    passed &= inspect(0.0f, 0.0f, 0.0f, false).disposition == Disposition::ExcludePrimaryMiss;
    passed &= inspect(2.0f, 1.0f, 2.0f, false).disposition == Disposition::InvalidData;
    passed &= inspect(0.0f, 4.0f, 0.0f, true, false).disposition == Disposition::InactiveLobe;
    passed &= inspect(0.0f, 4.0f, 0.0f, true, false).zeroRadianceRequired;
    passed &= inspect(0.0f, 4.0f, 0.0f).disposition == Disposition::InvalidData;
    passed &= inspect(0.0f, 2.0f, 100.0f, true, false).disposition == Disposition::InvalidData;
    passed &= inspect(0.0f, 1.5f, 0.0f).disposition == Disposition::InvalidData;
    passed &= inspect(0.0f, 8.0f, 0.0f).disposition == Disposition::InvalidData;
    passed &= inspect(std::numeric_limits<float>::quiet_NaN(), 1.0f, 1.0f).disposition == Disposition::InvalidData;
    passed &= Engine::InspectNrdRelaxDistance(1, 1, 1, 1, 100, true, true).disposition == Disposition::InvalidData;
    passed &= Engine::InspectNrdRelaxDistance(1, 1, 1, 0, 0, true, true).disposition == Disposition::InvalidData;
    if (!passed)
    {
        std::cerr << "NRD RELAX must remain unavailable until dispatch and inputs are integrated.\n";
    }
    return passed ? 0 : 1;
}
