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

}
