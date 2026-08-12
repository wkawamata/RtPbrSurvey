#include "stdafx.h"

#include "CommandLineOptions.h"

#include <cstdlib>
#include <cmath>
#include <cstring>
#include <cwchar>
#include <fstream>
#include <nlohmann/json.hpp>
#include <unordered_set>

namespace Platform
{
namespace
{

bool IsCommandLineArg(const WCHAR* arg, const WCHAR* expected)
{
    return _wcsicmp(arg, expected) == 0;
}

bool IsValidCaptureVariant(const std::string& variant)
{
    if (variant.empty())
    {
        return false;
    }

    for (const char character : variant)
    {
        const bool alphaNumeric =
            (character >= 'a' && character <= 'z') ||
            (character >= 'A' && character <= 'Z') ||
            (character >= '0' && character <= '9');
        if (!alphaNumeric && character != '-' && character != '_')
        {
            return false;
        }
    }
    return true;
}

bool HasParentTraversal(const std::filesystem::path& path)
{
    for (const std::filesystem::path& component : path)
    {
        if (component == L"..")
        {
            return true;
        }
    }
    return false;
}

bool ParseCapturePath(const std::filesystem::path& planDirectory,
                      const std::string& pathTemplate,
                      const std::string& variant,
                      std::filesystem::path& path,
                      std::string& error)
{
    static constexpr const char* kVariantToken = "{variant}";
    const size_t tokenPosition = pathTemplate.find(kVariantToken);
    if (tokenPosition == std::string::npos)
    {
        error = "Each capture path must contain the {variant} token.";
        return false;
    }

    std::string resolvedTemplate = pathTemplate;
    resolvedTemplate.replace(tokenPosition, strlen(kVariantToken), variant);
    if (resolvedTemplate.find(kVariantToken) != std::string::npos)
    {
        error = "Each capture path must contain exactly one {variant} token.";
        return false;
    }

    const std::u8string utf8Path(reinterpret_cast<const char8_t*>(resolvedTemplate.data()),
                                 resolvedTemplate.size());
    const std::filesystem::path relativePath(utf8Path);
    if (relativePath.is_absolute() || HasParentTraversal(relativePath))
    {
        error = "Capture paths must remain relative to the plan directory.";
        return false;
    }
    if (_wcsicmp(relativePath.extension().c_str(), L".png") != 0)
    {
        error = "Capture paths must use the .png extension.";
        return false;
    }

    path = planDirectory / relativePath;
    return true;
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
        else if (IsCommandLineArg(argv[i], L"-CaptureReflectionTemporalValidity"))
        {
            options.captureReflectionResolvedRadiance = true;
            options.captureReflectionTemporalValidity = true;
        }
        else if (IsCommandLineArg(argv[i], L"-ReflectionStochasticSampling"))
        {
            options.reflectionStochasticSampling = true;
        }
        else if (IsCommandLineArg(argv[i], L"-ReflectionCameraDistanceScale"))
        {
            if (i + 1 < argc)
            {
                WCHAR* end = nullptr;
                const float scale = wcstof(argv[++i], &end);
                if (end != argv[i] && *end == L'\0' && std::isfinite(scale) && scale > 0.0f)
                {
                    options.reflectionCameraDistanceScale = scale;
                }
            }
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
        else if (IsCommandLineArg(argv[i], L"-ReflectionOrbitDegrees"))
        {
            if (i + 1 < argc)
            {
                WCHAR* end = nullptr;
                const float degrees = wcstof(argv[++i], &end);
                if (end != argv[i] && *end == L'\0')
                {
                    options.reflectionOrbitDegrees = degrees;
                }
            }
        }
        else if (IsCommandLineArg(argv[i], L"-ReflectionOrbitFrames"))
        {
            if (i + 1 < argc)
            {
                const int frames = _wtoi(argv[++i]);
                if (frames > 0)
                {
                    options.reflectionOrbitFrames = static_cast<UINT>(frames);
                }
            }
        }
        else if (IsCommandLineArg(argv[i], L"-ReflectionTemporalNoiseStrength"))
        {
            if (i + 1 < argc)
            {
                WCHAR* end = nullptr;
                const float strength = wcstof(argv[++i], &end);
                if (end != argv[i] && *end == L'\0')
                {
                    options.hasReflectionTemporalNoiseStrength = true;
                    options.reflectionTemporalNoiseStrength = strength;
                }
            }
        }
        else if (IsCommandLineArg(argv[i], L"-ReflectionCapturePlan"))
        {
            if (i + 1 < argc)
            {
                options.reflectionCapturePlanPath = argv[++i];
                options.captureReflectionResolvedRadiance = true;
            }
        }
        else if (IsCommandLineArg(argv[i], L"-ReflectionCaptureVariant"))
        {
            if (i + 1 < argc)
            {
                const std::filesystem::path variantPath = argv[++i];
                options.reflectionCaptureVariant = variantPath.string();
            }
        }
    }

    return options;
}

bool LoadReflectionCapturePlan(const std::filesystem::path& path,
                               const std::string& variant,
                               ReflectionCapturePlan& plan,
                               std::string& error)
{
    plan = {};
    error.clear();

    if (!IsValidCaptureVariant(variant))
    {
        error = "Reflection capture variant must use only letters, digits, '-' or '_'.";
        return false;
    }

    try
    {
        std::ifstream input(path);
        if (!input)
        {
            error = "Unable to open reflection capture plan: " + path.string();
            return false;
        }

        const nlohmann::json document = nlohmann::json::parse(input);
        plan.version = document.at("version").get<UINT>();
        if (plan.version != 1)
        {
            error = "Unsupported reflection capture plan version.";
            return false;
        }

        const nlohmann::json& keyframes = document.at("cameraKeyframes");
        if (!keyframes.is_array() || keyframes.empty())
        {
            error = "Reflection capture plan requires cameraKeyframes.";
            return false;
        }
        for (const nlohmann::json& value : keyframes)
        {
            ReflectionCaptureCameraKeyframe keyframe = {};
            keyframe.frame = value.at("frame").get<UINT64>();
            keyframe.yawDegrees = value.at("yawDegrees").get<float>();
            if (!std::isfinite(keyframe.yawDegrees))
            {
                error = "Camera keyframe yawDegrees must be finite.";
                return false;
            }
            if (!plan.cameraKeyframes.empty() && keyframe.frame <= plan.cameraKeyframes.back().frame)
            {
                error = "Camera keyframe frames must be strictly increasing.";
                return false;
            }
            plan.cameraKeyframes.push_back(keyframe);
        }
        if (plan.cameraKeyframes.front().frame != 0)
        {
            error = "The first camera keyframe must use frame 0.";
            return false;
        }

        const nlohmann::json& captures = document.at("captures");
        if (!captures.is_array() || captures.empty())
        {
            error = "Reflection capture plan requires captures.";
            return false;
        }

        const std::filesystem::path absolutePlanPath = std::filesystem::absolute(path);
        const std::filesystem::path planDirectory = absolutePlanPath.parent_path();
        std::unordered_set<std::string> caseIds;
        for (const nlohmann::json& value : captures)
        {
            ReflectionCaptureRequest capture = {};
            capture.frame = value.at("frame").get<UINT64>();
            capture.caseId = value.at("caseId").get<std::string>();
            if (capture.caseId.empty())
            {
                error = "Capture caseId must not be empty.";
                return false;
            }
            if (!caseIds.insert(capture.caseId).second)
            {
                error = "Capture caseId values must be unique.";
                return false;
            }
            if (!plan.captures.empty() && capture.frame <= plan.captures.back().frame)
            {
                error = "Capture frames must be strictly increasing.";
                return false;
            }
            if (!ParseCapturePath(planDirectory,
                                  value.at("path").get<std::string>(),
                                  variant,
                                  capture.path,
                                  error))
            {
                return false;
            }
            plan.captures.push_back(std::move(capture));
        }
    }
    catch (const std::exception& exception)
    {
        error = "Invalid reflection capture plan: " + std::string(exception.what());
        plan = {};
        return false;
    }

    return true;
}

} // namespace Platform
