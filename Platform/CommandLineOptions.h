#pragma once

#include <Windows.h>

#include <string>

namespace Platform
{

struct CommandLineOptions
{
    bool useWarpDevice = false;
    std::wstring logFilePath;
    UINT logFpsInterval = 0;
    bool autoSelectGltfDamagedHelmet = false;
};

CommandLineOptions ParseCommandLineOptions(_In_reads_(argc) WCHAR* argv[], int argc);

} // namespace Platform
