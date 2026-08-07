#pragma once

#include <Windows.h>

#include <filesystem>
#include <string>

namespace Platform
{

struct CommandLineOptions
{
    bool useWarpDevice = false;
    std::wstring logFilePath;
    UINT logFpsInterval = 0;
    bool autoSelectGltfDamagedHelmet = false;
    std::filesystem::path capturePath;
    UINT captureAfterFrames = 0;
    bool exitAfterCapture = false;
    bool captureReflectionResolvedRadiance = false;
    bool hasReflectionTemporalWeight = false;
    float reflectionTemporalWeight = 0.0f;
    float reflectionOrbitDegrees = 0.0f;
    UINT reflectionOrbitFrames = 0;
    bool hasReflectionTemporalNoiseStrength = false;
    float reflectionTemporalNoiseStrength = 0.0f;
};

CommandLineOptions ParseCommandLineOptions(_In_reads_(argc) WCHAR* argv[], int argc);

} // namespace Platform
