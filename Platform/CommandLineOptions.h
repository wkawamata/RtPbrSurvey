#pragma once

#include <Windows.h>

#include <filesystem>
#include <string>
#include <vector>

namespace Platform
{

struct ReflectionCaptureCameraKeyframe
{
    UINT64 frame = 0;
    float yawDegrees = 0.0f;
};

struct ReflectionCaptureRequest
{
    UINT64 frame = 0;
    std::string caseId;
    std::filesystem::path path;
};

struct ReflectionCapturePlan
{
    UINT version = 0;
    std::vector<ReflectionCaptureCameraKeyframe> cameraKeyframes;
    std::vector<ReflectionCaptureRequest> captures;
};

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
    bool reflectionStochasticSampling = false;
    float reflectionCameraDistanceScale = 1.0f;
    bool hasReflectionTemporalWeight = false;
    float reflectionTemporalWeight = 0.0f;
    float reflectionOrbitDegrees = 0.0f;
    UINT reflectionOrbitFrames = 0;
    bool hasReflectionTemporalNoiseStrength = false;
    float reflectionTemporalNoiseStrength = 0.0f;
    std::filesystem::path reflectionCapturePlanPath;
    std::string reflectionCaptureVariant;
};

CommandLineOptions ParseCommandLineOptions(_In_reads_(argc) WCHAR* argv[], int argc);
bool LoadReflectionCapturePlan(const std::filesystem::path& path,
                               const std::string& variant,
                               ReflectionCapturePlan& plan,
                               std::string& error);

} // namespace Platform
