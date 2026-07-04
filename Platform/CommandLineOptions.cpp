#include "stdafx.h"

#include "CommandLineOptions.h"

#include <cstdlib>
#include <cwchar>

namespace Platform
{
namespace
{

bool IsCommandLineArg(const WCHAR* arg, const WCHAR* expected)
{
    return _wcsicmp(arg, expected) == 0;
}

} // namespace

_Use_decl_annotations_ CommandLineOptions ParseCommandLineOptions(WCHAR* argv[], int argc)
{
    CommandLineOptions options = {};

    for (int i = 1; i < argc; ++i)
    {
        if (IsCommandLineArg(argv[i], L"-warp") || IsCommandLineArg(argv[i], L"/warp"))
        {
            options.useWarpDevice = true;
        }
        else if (IsCommandLineArg(argv[i], L"-LogToFile"))
        {
            if (i + 1 < argc)
            {
                options.logFilePath = argv[++i];
            }
        }
        else if (IsCommandLineArg(argv[i], L"-LogFPS"))
        {
            if (i + 1 < argc)
            {
                const int logFpsInterval = _wtoi(argv[++i]);
                if (logFpsInterval > 0)
                {
                    options.logFpsInterval = static_cast<UINT>(logFpsInterval);
                }
            }
        }
        else if (IsCommandLineArg(argv[i], L"-AutoSelectGltfDamagedHelmet"))
        {
            options.autoSelectGltfDamagedHelmet = true;
        }
    }

    return options;
}

} // namespace Platform
