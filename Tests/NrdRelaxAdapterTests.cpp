#include "Renderer/NrdRelaxAdapter.h"

#include <cstring>
#include <iostream>

int main()
{
    const Engine::NrdRelaxSupportInfo defaults;
    const Engine::NrdRelaxSupportInfo info = Engine::QueryNrdRelaxSupport();
    const bool passed = !defaults.available && !info.available &&
        defaults.status == Engine::NrdRelaxSupportStatus::NotIntegrated &&
        info.status == Engine::NrdRelaxSupportStatus::NotIntegrated &&
        info.targetSdkVersion != nullptr && std::strcmp(info.targetSdkVersion, "4.17.3") == 0 &&
        info.reason != nullptr && info.reason[0] != '\0';
    if (!passed)
    {
        std::cerr << "NRD RELAX must remain unavailable until dispatch and inputs are integrated.\n";
    }
    return passed ? 0 : 1;
}
