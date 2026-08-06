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
        else if (IsCommandLineArg(argv[i], L"-CapturePath"))
        {
            if (i + 1 < argc)
            {
                options.capturePath = argv[++i];
            }
        }
        else if (IsCommandLineArg(argv[i], L"-CaptureAfterFrames"))
        {
            if (i + 1 < argc)
            {
                const int captureAfterFrames = _wtoi(argv[++i]);
                if (captureAfterFrames >= 0)
                {
                    options.captureAfterFrames = static_cast<UINT>(captureAfterFrames);
                }
            }
        }
        else if (IsCommandLineArg(argv[i], L"-ExitAfterCapture"))
        {
            options.exitAfterCapture = true;
        }
        else if (IsCommandLineArg(argv[i], L"-CaptureReflectionResolvedRadiance"))
        {
            options.captureReflectionResolvedRadiance = true;
        }
        else if (IsCommandLineArg(argv[i], L"-ReflectionTemporalWeight"))
        {
            if (i + 1 < argc)
            {
                WCHAR* end = nullptr;
                const float weight = wcstof(argv[++i], &end);
                if (end != argv[i] && *end == L'\0')
                {
                    options.hasReflectionTemporalWeight = true;
                    options.reflectionTemporalWeight = weight;
                }
            }
        }
    }

    return options;
}

} // namespace Platform
