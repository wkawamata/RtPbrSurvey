#include "stdafx.h"

#include "CommandLineOptions.h"

#include "Runtime/CaptureSession.h"

#include <cerrno>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <cwchar>
#include <fstream>
#include <limits>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <unordered_set>

namespace Platform
{
namespace
{

bool IsCommandLineArg(const WCHAR* arg, const WCHAR* expected)
{
    return _wcsicmp(arg, expected) == 0;
}

bool TryParseUint(const WCHAR* value, bool allowZero, UINT& result)
{
    if (value == nullptr || value[0] == L'\0' || value[0] == L'-')
    {
        return false;
    }

    wchar_t* end = nullptr;
    errno = 0;
    const unsigned long long parsed = wcstoull(value, &end, 10);
    if (errno == ERANGE || end == value || *end != L'\0' ||
        parsed > static_cast<unsigned long long>((std::numeric_limits<UINT>::max)()) ||
        (!allowZero && parsed == 0))
    {
        return false;
    }

    result = static_cast<UINT>(parsed);
    return true;
}

bool TryParsePositiveDouble(const WCHAR* value, double& result)
{
    if (value == nullptr || value[0] == L'\0')
    {
        return false;
    }

    wchar_t* end = nullptr;
    errno = 0;
    const double parsed = wcstod(value, &end);
    if (errno == ERANGE || end == value || *end != L'\0' || !std::isfinite(parsed) || parsed <= 0.0)
    {
        return false;
    }

    result = parsed;
    return true;
}

std::string WideToUtf8(const std::wstring& value)
{
    if (value.empty())
    {
        return {};
    }

    const int length = WideCharToMultiByte(
        CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (length <= 0)
    {
        throw std::runtime_error("Failed to convert command-line text to UTF-8.");
    }
    std::string result(static_cast<size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), result.data(), length, nullptr, nullptr);
    return result;
}

bool TryParseCaptureSessionFormat(const std::wstring& value, RtPbrSurvey::CaptureSessionOutputFormat& format)
{
    if (_wcsicmp(value.c_str(), L"png") == 0)
    {
        format = RtPbrSurvey::CaptureSessionOutputFormat::Png;
        return true;
    }
    if (_wcsicmp(value.c_str(), L"exr") == 0)
    {
        format = RtPbrSurvey::CaptureSessionOutputFormat::Exr;
        return true;
    }
    if (_wcsicmp(value.c_str(), L"gif") == 0)
    {
        format = RtPbrSurvey::CaptureSessionOutputFormat::Gif;
        return true;
    }
    if (_wcsicmp(value.c_str(), L"mp4") == 0)
    {
        format = RtPbrSurvey::CaptureSessionOutputFormat::Mp4;
        return true;
    }
    return false;
}

bool TryParseCaptureSessionGifRepeat(const std::wstring& value,
                                     RtPbrSurvey::CaptureSessionGifRepeatMode& mode,
                                     std::uint16_t& count)
{
    if (_wcsicmp(value.c_str(), L"none") == 0)
    {
        mode = RtPbrSurvey::CaptureSessionGifRepeatMode::None;
        count = 0;
        return true;
    }
    if (_wcsicmp(value.c_str(), L"infinite") == 0)
    {
        mode = RtPbrSurvey::CaptureSessionGifRepeatMode::Infinite;
        count = 0;
        return true;
    }

    UINT parsedCount = 0;
    if (!TryParseUint(value.c_str(), false, parsedCount) || parsedCount > (std::numeric_limits<std::uint16_t>::max)())
    {
        return false;
    }
    mode = RtPbrSurvey::CaptureSessionGifRepeatMode::Count;
    count = static_cast<std::uint16_t>(parsedCount);
    return true;
}

bool TryParseCaptureSessionGifDisposal(const std::wstring& value, RtPbrSurvey::CaptureSessionGifDisposal& disposal)
{
    if (_wcsicmp(value.c_str(), L"keep") == 0)
    {
        disposal = RtPbrSurvey::CaptureSessionGifDisposal::Keep;
        return true;
    }
    if (_wcsicmp(value.c_str(), L"background") == 0)
    {
        disposal = RtPbrSurvey::CaptureSessionGifDisposal::Background;
        return true;
    }
    if (_wcsicmp(value.c_str(), L"previous") == 0)
    {
        disposal = RtPbrSurvey::CaptureSessionGifDisposal::Previous;
        return true;
    }
    return false;
}

bool TryParseDebugResourceName(const WCHAR* value, std::string& resourceName)
{
    resourceName.clear();
    for (const WCHAR* character = value; *character != L'\0'; ++character)
    {
        if (*character < 0x20 || *character > 0x7e)
        {
            resourceName.clear();
            return false;
        }
        resourceName.push_back(static_cast<char>(*character));
    }
    return !resourceName.empty();
}

bool TryParseDlssSrQualityMode(const WCHAR* value, DlssSrQualityMode& qualityMode)
{
    struct QualityModeName
    {
        const WCHAR* name;
        DlssSrQualityMode qualityMode;
    };

    static constexpr QualityModeName kQualityModeNames[] = {
        {L"dlaa", DlssSrQualityMode::Dlaa},
        {L"quality", DlssSrQualityMode::Quality},
        {L"balanced", DlssSrQualityMode::Balanced},
        {L"performance", DlssSrQualityMode::Performance},
        {L"ultra-performance", DlssSrQualityMode::UltraPerformance},
    };

    for (const QualityModeName& entry : kQualityModeNames)
    {
        if (_wcsicmp(value, entry.name) == 0)
        {
            qualityMode = entry.qualityMode;
            return true;
        }
    }
    return false;
}

bool TryParseReflectionCaptureDebugView(const WCHAR* value, ReflectionCaptureDebugView& debugView)
{
    struct DebugViewName
    {
        const WCHAR* name;
        ReflectionCaptureDebugView debugView;
    };

    static constexpr DebugViewName kDebugViewNames[] = {
        {L"lit", ReflectionCaptureDebugView::Lit},
        {L"resolved-radiance", ReflectionCaptureDebugView::ResolvedRadiance},
        {L"temporal-validity", ReflectionCaptureDebugView::TemporalValidity},
        {L"albedo", ReflectionCaptureDebugView::GBufferAlbedo},
        {L"pbr-params", ReflectionCaptureDebugView::GBufferPbrParams},
        {L"normal", ReflectionCaptureDebugView::GBufferNormal},
        {L"motion-vector", ReflectionCaptureDebugView::GBufferMotionVector},
        {L"depth", ReflectionCaptureDebugView::Depth},
        {L"specular-albedo", ReflectionCaptureDebugView::RayReconstructionSpecularAlbedo},
        {L"roughness", ReflectionCaptureDebugView::RayReconstructionRoughness},
        {L"specular-hit-distance", ReflectionCaptureDebugView::RayReconstructionSpecularHitDistance},
        {L"hit-material", ReflectionCaptureDebugView::ReflectionRayMaterial},
        {L"evaluated-radiance", ReflectionCaptureDebugView::EvaluatedRadiance},
        {L"specular-estimate", ReflectionCaptureDebugView::SpecularEstimate},
        {L"resolved-specular-estimate", ReflectionCaptureDebugView::ResolvedSpecularEstimate},
        {L"specular-variance", ReflectionCaptureDebugView::SpecularVariance},
    };

    for (const DebugViewName& entry : kDebugViewNames)
    {
        if (_wcsicmp(value, entry.name) == 0)
        {
            debugView = entry.debugView;
            return true;
        }
    }
    return false;
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
                      const wchar_t* requiredExtension,
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
    if (_wcsicmp(relativePath.extension().c_str(), requiredExtension) != 0)
    {
        error = _wcsicmp(requiredExtension, L".png") == 0 ? "Capture paths must use the .png extension."
                                                          : "Path Tracing capture paths must use the .ptbuf extension.";
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
        else if (IsCommandLineArg(argv[i], L"-WindowSize"))
        {
            if (i + 2 >= argc || !TryParseUint(argv[++i], false, options.windowWidth) ||
                !TryParseUint(argv[++i], false, options.windowHeight) ||
                options.windowWidth < 64 || options.windowHeight < 64 ||
                options.windowWidth > 4096 || options.windowHeight > 4096)
            {
                throw std::invalid_argument("-WindowSize expects width height in [64, 4096].");
            }
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
        else if (IsCommandLineArg(argv[i], L"-BenchmarkDrawOrder"))
        {
            if (i + 1 >= argc || (_wcsicmp(argv[i + 1], L"forward") != 0 && _wcsicmp(argv[i + 1], L"reverse") != 0))
            {
                throw std::invalid_argument("-BenchmarkDrawOrder expects forward or reverse.");
            }
            options.benchmarkReverseDrawOrder = _wcsicmp(argv[++i], L"reverse") == 0;
        }
        else if (IsCommandLineArg(argv[i], L"-BenchmarkDepthPrepass"))
        {
            if (i + 1 >= argc || (_wcsicmp(argv[i + 1], L"on") != 0 && _wcsicmp(argv[i + 1], L"off") != 0))
            {
                throw std::invalid_argument("-BenchmarkDepthPrepass expects on or off.");
            }
            options.benchmarkDepthPrepass = _wcsicmp(argv[++i], L"on") == 0;
        }
        else if (IsCommandLineArg(argv[i], L"-BenchmarkGBuffer"))
        {
            if (i + 1 >= argc || (_wcsicmp(argv[i + 1], L"full") != 0 && _wcsicmp(argv[i + 1], L"constant") != 0))
            {
                throw std::invalid_argument("-BenchmarkGBuffer expects full or constant.");
            }
            options.benchmarkConstantGBuffer = _wcsicmp(argv[++i], L"constant") == 0;
        }
        else if (IsCommandLineArg(argv[i], L"-BenchmarkInstancing"))
        {
            if (i + 1 >= argc || (_wcsicmp(argv[i + 1], L"on") != 0 && _wcsicmp(argv[i + 1], L"off") != 0))
            {
                throw std::invalid_argument("-BenchmarkInstancing expects on or off.");
            }
            options.benchmarkInstancing = _wcsicmp(argv[++i], L"on") == 0;
        }
        else if (IsCommandLineArg(argv[i], L"-BenchmarkMode"))
        {
            if (i + 1 >= argc || (_wcsicmp(argv[i + 1], L"raster") != 0 && _wcsicmp(argv[i + 1], L"hybrid") != 0))
            {
                throw std::invalid_argument("-BenchmarkMode expects raster or hybrid.");
            }
            options.benchmarkHybrid = _wcsicmp(argv[++i], L"hybrid") == 0;
        }
        else if (IsCommandLineArg(argv[i], L"-BenchmarkReport"))
        {
            if (i + 1 >= argc || argv[i + 1][0] == L'\0')
            {
                throw std::invalid_argument("-BenchmarkReport expects a JSON output path.");
            }
            options.benchmarkReportPath = argv[++i];
        }
        else if (IsCommandLineArg(argv[i], L"-BenchmarkInstances") ||
                 IsCommandLineArg(argv[i], L"-BenchmarkWarmupFrames") ||
                 IsCommandLineArg(argv[i], L"-BenchmarkFrames"))
        {
            const bool instances = IsCommandLineArg(argv[i], L"-BenchmarkInstances");
            const bool warmup = IsCommandLineArg(argv[i], L"-BenchmarkWarmupFrames");
            UINT value = 0;
            if (i + 1 >= argc || !TryParseUint(argv[++i], warmup || instances, value) ||
                value > (instances ? 1000u : 1000000u))
            {
                throw std::invalid_argument("Invalid benchmark frame/instance count.");
            }
            if (instances)
            {
                options.benchmarkInstances = value;
            }
            else if (warmup)
            {
                options.benchmarkWarmupFrames = value;
            }
            else
            {
                options.benchmarkFrames = value;
            }
        }
        else if (IsCommandLineArg(argv[i], L"-AutoSelectGltfDamagedHelmet"))
        {
            options.autoSelectGltfDamagedHelmet = true;
        }
        else if (IsCommandLineArg(argv[i], L"-AutoSelectGltfAsset"))
        {
            if (i + 1 < argc)
            {
                options.autoSelectGltfAssetName = argv[++i];
            }
        }
        else if (IsCommandLineArg(argv[i], L"-AutoSelectHybridReflectionEstimatorTest"))
        {
            options.autoSelectHybridReflectionEstimatorTest = true;
        }
        else if (IsCommandLineArg(argv[i], L"-EvaluationCase"))
        {
            if (i + 1 >= argc || argv[i + 1][0] == L'\0')
            {
                throw std::invalid_argument("-EvaluationCase expects a saved Evaluation Case name.");
            }
            options.evaluationCaseName = argv[++i];
        }
        else if (IsCommandLineArg(argv[i], L"-SceneFile"))
        {
            if (i + 1 >= argc || argv[i + 1][0] == L'\0')
            {
                throw std::invalid_argument("-SceneFile expects a scene.json path.");
            }
            options.sceneFilePath = argv[++i];
        }
        else if (IsCommandLineArg(argv[i], L"-RenderPreset"))
        {
            if (i + 1 >= argc || argv[i + 1][0] == L'\0')
            {
                throw std::invalid_argument("-RenderPreset expects a preset JSON path.");
            }
            options.renderPresetPath = argv[++i];
        }
        else if (IsCommandLineArg(argv[i], L"-UseSceneDefaults"))
        {
            options.useSceneDefaults = true;
        }
        else if (IsCommandLineArg(argv[i], L"-EnableDlssSr"))
        {
            options.enableDlssSr = true;
        }
        else if (IsCommandLineArg(argv[i], L"-DisableVSync"))
        {
            options.disableVSync = true;
        }
        else if (IsCommandLineArg(argv[i], L"-DisablePresentTearing"))
        {
            options.disablePresentTearing = true;
        }
        else if (IsCommandLineArg(argv[i], L"-DisableD3D12DebugLayer"))
        {
            options.disableD3D12DebugLayer = true;
        }
        else if (IsCommandLineArg(argv[i], L"-EnablePathTracing"))
        {
            options.enablePathTracing = true;
        }
        else if (IsCommandLineArg(argv[i], L"-StreamlineLogDirectory"))
        {
            if (i + 1 >= argc || argv[i + 1][0] == L'\0')
            {
                throw std::invalid_argument("-StreamlineLogDirectory requires an existing directory");
            }
            options.streamlineLogDirectory = std::filesystem::absolute(argv[++i]);
            if (!std::filesystem::is_directory(options.streamlineLogDirectory))
            {
                throw std::invalid_argument("-StreamlineLogDirectory requires an existing directory");
            }
        }
        else if (IsCommandLineArg(argv[i], L"-RrCrashLog"))
        {
            if (i + 1 >= argc || argv[i + 1][0] == L'\0' || argv[i + 1][0] == L'-')
            {
                throw std::invalid_argument("-RrCrashLog requires a log file path");
            }
            options.rrCrashLogPath = argv[++i];
        }
        else if (IsCommandLineArg(argv[i], L"-RrFenceTrace"))
        {
            if (i + 1 >= argc)
                throw std::invalid_argument("-RrFenceTrace requires a path");
            options.rrFenceTracePath = argv[++i];
        }
        else if (IsCommandLineArg(argv[i], L"-RrFenceTraceSeconds") ||
                 IsCommandLineArg(argv[i], L"-RrFenceTraceWarmup"))
        {
            const bool warmup = IsCommandLineArg(argv[i], L"-RrFenceTraceWarmup");
            if (i + 1 >= argc)
                throw std::invalid_argument("Trace duration requires seconds");
            UINT seconds = 0;
            if (!TryParseUint(argv[++i], false, seconds) || seconds > 120)
                throw std::invalid_argument("Trace seconds must be 1..120");
            if (warmup)
                options.rrFenceTraceWarmup = static_cast<UINT>(seconds);
            else
                options.rrFenceTraceSeconds = static_cast<UINT>(seconds);
        }
        else if (IsCommandLineArg(argv[i], L"-RrFenceTraceTimingOnly"))
        {
            options.rrFenceTraceTimingOnly = true;
        }
        else if (IsCommandLineArg(argv[i], L"-RrFenceTraceCalibrateVerifiedGpu"))
        {
            options.rrFenceTraceCalibrateVerifiedGpu = true;
        }
        else if (IsCommandLineArg(argv[i], L"-RrFenceTraceVSyncCycle"))
        {
            options.rrFenceTraceVSyncCycle = true;
        }
        else if (IsCommandLineArg(argv[i], L"-RrFenceTraceNonblockingCycle"))
        {
            options.rrFenceTraceNonblockingCycle = true;
        }
        else if (IsCommandLineArg(argv[i], L"-RrFenceTraceCameraMotion"))
        {
            options.rrFenceTraceCameraMotion = true;
        }
        else if (IsCommandLineArg(argv[i], L"-NonblockingFrameAcquisition"))
        {
            options.nonblockingFrameAcquisition = true;
        }
        else if (IsCommandLineArg(argv[i], L"-PathTracingRrSchedule"))
        {
            if (i + 1 >= argc || (!IsCommandLineArg(argv[i + 1], L"safe-serial") &&
                                  !IsCommandLineArg(argv[i + 1], L"chained")))
                throw std::invalid_argument("-PathTracingRrSchedule expects safe-serial or chained.");
            options.pathTracingRrChained = IsCommandLineArg(argv[++i], L"chained");
            options.pathTracingRrSafeSerial = true;
        }
        else if (IsCommandLineArg(argv[i], L"-PathTracingRrSeparateCompletionFence"))
        {
            options.pathTracingRrSeparateCompletionFence = true;
        }
        else if (IsCommandLineArg(argv[i], L"-PathTracingRrVerifiedPresent"))
        {
            options.pathTracingRrVerifiedPresent = true;
        }
        else if (IsCommandLineArg(argv[i], L"-PathTracingPrimaryGlass16Bounces"))
        {
            options.pathTracingPrimaryGlass16Bounces = true;
        }
        else if (IsCommandLineArg(argv[i], L"-PathTracingRrStrictSchedule"))
        {
            options.pathTracingRrStrictSchedule = true;
        }
        else if (IsCommandLineArg(argv[i], L"-PathTracingRrContinuous"))
        {
            options.enablePathTracing = true;
            options.pathTracingReconstruction = "rr";
            options.pathTracingRrContinuous = true;
        }
        else if (IsCommandLineArg(argv[i], L"-PathTracingRrThinExperiment"))
        {
            options.enablePathTracing = true;
            options.pathTracingReconstruction = "rr";
            options.pathTracingRrContinuous = true;
            options.pathTracingRrThinExperiment = true;
        }
        else if (IsCommandLineArg(argv[i], L"-PathTracingRrSmoothVolumeExperiment"))
        {
            options.enablePathTracing = true;
            options.pathTracingReconstruction = "rr";
            options.pathTracingRrContinuous = true;
            options.pathTracingRrSmoothVolumeExperiment = true;
        }
        else if (IsCommandLineArg(argv[i], L"-PathTracingRrRoughVolumeExperiment"))
        {
            options.enablePathTracing = true;
            options.pathTracingReconstruction = "rr";
            options.pathTracingRrContinuous = true;
            options.pathTracingRrRoughVolumeExperiment = true;
        }
        else if (IsCommandLineArg(argv[i], L"-PathTracingRrTransmissionMotionExperiment"))
        {
            options.pathTracingRrTransmissionMotionExperiment = true;
        }
        else if (IsCommandLineArg(argv[i], L"-PathTracingRrInputCapture"))
        {
            if (i + 1 >= argc || argv[i + 1][0] == L'-')
                throw std::invalid_argument("-PathTracingRrInputCapture expects an output directory.");
            options.pathTracingRrInputCaptureDirectory = argv[++i];
            options.enablePathTracing = true;
            options.pathTracingReconstruction = "rr";
            options.pathTracingRrContinuous = true;
        }
        else if (IsCommandLineArg(argv[i], L"-PathTracingRrRecoveryMaxCheckpoints"))
        {
            if (i + 1 >= argc || !TryParseUint(argv[++i], false, options.pathTracingRrRecoveryMaxCheckpoints) ||
                options.pathTracingRrRecoveryMaxCheckpoints > 31)
                throw std::invalid_argument("-PathTracingRrRecoveryMaxCheckpoints expects 1..31.");
        }
        else if (IsCommandLineArg(argv[i], L"-PathTracingRrComparisonCamera"))
        {
            options.pathTracingRrComparisonCamera = true;
        }
        else if (IsCommandLineArg(argv[i], L"-PathTracingRrComparisonCapture"))
        {
            if (i + 1 >= argc || argv[i + 1][0] == L'-')
                throw std::invalid_argument("-PathTracingRrComparisonCapture expects a directory.");
            options.pathTracingRrComparisonCaptureDirectory = argv[++i];
            options.enablePathTracing = true;
            options.pathTracingReconstruction = "rr";
            options.pathTracingRrContinuous = true;
        }
        else if (IsCommandLineArg(argv[i], L"-PathTracingRrRegressionValidation"))
        {
            if (i + 1 >= argc || argv[i + 1][0] == L'-')
                throw std::invalid_argument("-PathTracingRrRegressionValidation expects an output directory.");
            options.pathTracingRrRegressionValidationDirectory = argv[++i];
            options.enablePathTracing = true;
            options.pathTracingReconstruction = "rr";
            options.pathTracingRrContinuous = true;
            options.pathTracingRrSafeSerial = options.pathTracingRrChained = true;
        }
        else if (IsCommandLineArg(argv[i], L"-PathTracingRrRecoveryValidation"))
        {
            if (i + 1 >= argc || argv[i + 1][0] == L'-')
                throw std::invalid_argument("-PathTracingRrRecoveryValidation expects an output directory.");
            options.pathTracingRrRecoveryValidationDirectory = argv[++i];
            options.enablePathTracing = true;
            options.pathTracingReconstruction = "rr";
            options.pathTracingRrContinuous = true;
            options.pathTracingRrSafeSerial = options.pathTracingRrChained = true;
        }
        else if (IsCommandLineArg(argv[i], L"-PathTracingRrLifecycleValidation"))
        {
            if (i + 1 >= argc || argv[i + 1][0] == L'-')
                throw std::invalid_argument("-PathTracingRrLifecycleValidation expects an output directory.");
            options.pathTracingRrLifecycleValidationDirectory = argv[++i];
            options.enablePathTracing = true;
            options.pathTracingReconstruction = "rr";
            options.pathTracingRrContinuous = true;
        }
        else if (IsCommandLineArg(argv[i], L"-PathTracingReconstruction"))
        {
            if (i + 1 >= argc || (!IsCommandLineArg(argv[i + 1], L"native") && !IsCommandLineArg(argv[i + 1], L"nrd") &&
                                  !IsCommandLineArg(argv[i + 1], L"rr")))
                throw std::invalid_argument("-PathTracingReconstruction expects native, nrd, or rr.");
            options.pathTracingReconstruction = IsCommandLineArg(argv[i + 1], L"nrd")  ? "nrd"
                                                : IsCommandLineArg(argv[i + 1], L"rr") ? "rr"
                                                                                       : "native";
            ++i;
            options.enablePathTracing = true;
        }
        else if (IsCommandLineArg(argv[i], L"-PathTracingSamples"))
        {
            if (i + 1 >= argc || !TryParseUint(argv[++i], false, options.pathTracingSampleTarget))
            {
                throw std::invalid_argument("-PathTracingSamples expects an integer in [1, UINT_MAX].");
            }
            options.enablePathTracing = true;
        }
        else if (IsCommandLineArg(argv[i], L"-PathTracingHistoryValidation"))
        {
            if (i + 1 >= argc || argv[i + 1][0] == L'\0')
            {
                throw std::invalid_argument("-PathTracingHistoryValidation expects an output directory.");
            }
            options.pathTracingHistoryValidationDirectory = argv[++i];
            options.enablePathTracing = true;
        }
        else if (IsCommandLineArg(argv[i], L"-PathTracingEnvironmentMode"))
        {
            if (i + 1 >= argc || !TryParseUint(argv[++i], true, options.pathTracingEnvironmentMode) ||
                options.pathTracingEnvironmentMode > 7)
            {
                throw std::invalid_argument("-PathTracingEnvironmentMode expects 0..7: map BSDF, constant BSDF, constant NEE, map uniform NEE, map importance NEE, constant MIS, map uniform MIS, map importance MIS.");
            }
            options.hasPathTracingEnvironmentMode = true;
            options.enablePathTracing = true;
        }
        else if (IsCommandLineArg(argv[i], L"-PathTracingSeed"))
        {
            if (i + 1 >= argc || !TryParseUint(argv[++i], true, options.pathTracingRandomSeed))
            {
                throw std::invalid_argument("-PathTracingSeed expects an integer in [0, UINT_MAX].");
            }
            options.hasPathTracingRandomSeed = true;
            options.enablePathTracing = true;
        }
        else if (IsCommandLineArg(argv[i], L"-NrdDiagnosticFailure"))
        {
            if (i + 1 >= argc)
                throw std::invalid_argument(
                    "-NrdDiagnosticFailure expects initialization, last-dispatch or after-four-frames.");
            const std::wstring value = argv[++i];
            if (value == L"initialization")
                options.nrdDiagnosticFailure = "initialization";
            else if (value == L"last-dispatch")
                options.nrdDiagnosticFailure = "last-dispatch";
            else if (value == L"after-four-frames")
                options.nrdDiagnosticFailure = "after-four-frames";
            else
                throw std::invalid_argument("Unknown -NrdDiagnosticFailure mode.");
            options.enablePathTracing = true;
        }
        else if (IsCommandLineArg(argv[i], L"-PathTracingReferenceComponent"))
        {
            if (i + 1 >= argc)
                throw std::invalid_argument("-PathTracingReferenceComponent expects diffuse, specular, stable or background.");
            const std::wstring value = argv[++i];
            if (value == L"diffuse")
                options.pathTracingReferenceComponent = "diffuse";
            else if (value == L"specular")
                options.pathTracingReferenceComponent = "specular";
            else if (value == L"stable")
                options.pathTracingReferenceComponent = "stable";
            else if (value == L"background")
                options.pathTracingReferenceComponent = "background";
            else
                throw std::invalid_argument("Unknown -PathTracingReferenceComponent.");
            options.enablePathTracing = true;
        }
        else if (IsCommandLineArg(argv[i], L"-NrdPrepassProfile"))
        {
            if (i + 1 >= argc)
                throw std::invalid_argument("-NrdPrepassProfile expects control, specular-10 or diffuse-5.");
            const std::wstring value = argv[++i];
            if (value == L"control")
                options.nrdPrepassProfile = "control";
            else if (value == L"specular-10")
                options.nrdPrepassProfile = "specular-10";
            else if (value == L"diffuse-5")
                options.nrdPrepassProfile = "diffuse-5";
            else
                throw std::invalid_argument("Unknown -NrdPrepassProfile mode.");
            options.enablePathTracing = true;
            options.nrdPreview = true;
        }
        else if (IsCommandLineArg(argv[i], L"-NrdDisableAntilag"))
        {
            options.nrdDisableAntilag = true;
            options.nrdPreview = true;
            options.enablePathTracing = true;
        }
        else if (IsCommandLineArg(argv[i], L"-NrdPreview"))
        {
            options.nrdPreview = true;
            options.enablePathTracing = true;
        }
        else if (IsCommandLineArg(argv[i], L"-NrdContinuousCameraHistory"))
        {
            options.nrdContinuousCameraHistory = true;
            options.enablePathTracing = true;
        }
        else if (IsCommandLineArg(argv[i], L"-PathTracingObjectMotionX"))
        {
            if (i + 1 >= argc)
            {
                throw std::invalid_argument("-PathTracingObjectMotionX expects a finite delta in [-1, 1].");
            }
            wchar_t* end = nullptr;
            errno = 0;
            const WCHAR* value = argv[++i];
            const double delta = wcstod(value, &end);
            if (errno == ERANGE || end == value || *end != L'\0' || !std::isfinite(delta) || std::abs(delta) > 1.0)
            {
                throw std::invalid_argument("-PathTracingObjectMotionX expects a finite delta in [-1, 1].");
            }
            options.pathTracingObjectMotionX = static_cast<float>(delta);
            options.hasPathTracingObjectMotion = true;
            options.enablePathTracing = true;
        }
        else if (IsCommandLineArg(argv[i], L"-EnableDebugTexturePreview"))
        {
            options.enableDebugTexturePreview = true;
        }
        else if (IsCommandLineArg(argv[i], L"-DebugPreviewResource"))
        {
            if (i + 1 >= argc || !TryParseDebugResourceName(argv[++i], options.debugPreviewResourceName))
            {
                throw std::invalid_argument("-DebugPreviewResource expects a non-empty ASCII resource name.");
            }
            options.enableDebugTexturePreview = true;
        }
        else if (IsCommandLineArg(argv[i], L"-DlssQuality"))
        {
            if (i + 1 >= argc || !TryParseDlssSrQualityMode(argv[++i], options.dlssSrQualityMode))
            {
                throw std::invalid_argument(
                    "-DlssQuality expects dlaa, quality, balanced, performance, or ultra-performance.");
            }
            options.enableDlssSr = true;
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
        else if (IsCommandLineArg(argv[i], L"-CaptureSessionOutputDir"))
        {
            if (i + 1 >= argc || argv[i + 1][0] == L'\0')
            {
                throw std::invalid_argument("-CaptureSessionOutputDir expects a directory path.");
            }
            options.captureSessionEnabled = true;
            options.captureSessionOutputDirectory = argv[++i];
        }
        else if (IsCommandLineArg(argv[i], L"-CaptureSessionBaseName"))
        {
            if (i + 1 >= argc || argv[i + 1][0] == L'\0')
            {
                throw std::invalid_argument("-CaptureSessionBaseName expects a non-empty name.");
            }
            options.captureSessionEnabled = true;
            options.captureSessionBaseName = argv[++i];
        }
        else if (IsCommandLineArg(argv[i], L"-CaptureSessionSubfolder"))
        {
            if (i + 1 >= argc || argv[i + 1][0] == L'\0')
            {
                throw std::invalid_argument("-CaptureSessionSubfolder expects a non-empty relative path.");
            }
            options.captureSessionEnabled = true;
            options.captureSessionOutputSubdirectory = argv[++i];
        }
        else if (IsCommandLineArg(argv[i], L"-CaptureSessionFormat"))
        {
            if (i + 1 >= argc || argv[i + 1][0] == L'\0')
            {
                throw std::invalid_argument("-CaptureSessionFormat expects png, exr, gif, or mp4.");
            }
            options.captureSessionEnabled = true;
            options.captureSessionFormat = argv[++i];
        }
        else if (IsCommandLineArg(argv[i], L"-CaptureSessionGifRepeat"))
        {
            if (i + 1 >= argc || argv[i + 1][0] == L'\0')
            {
                throw std::invalid_argument("-CaptureSessionGifRepeat expects none, infinite, or a repeat count in [1, 65535].");
            }
            options.captureSessionEnabled = true;
            options.captureSessionGifRepeat = argv[++i];
        }
        else if (IsCommandLineArg(argv[i], L"-CaptureSessionGifDisposal"))
        {
            if (i + 1 >= argc || argv[i + 1][0] == L'\0')
            {
                throw std::invalid_argument("-CaptureSessionGifDisposal expects keep, background, or previous.");
            }
            options.captureSessionEnabled = true;
            options.captureSessionGifDisposal = argv[++i];
        }
        else if (IsCommandLineArg(argv[i], L"-CaptureSessionFps"))
        {
            if (i + 1 >= argc || !TryParseUint(argv[++i], false, options.captureSessionFramesPerSecond))
            {
                throw std::invalid_argument("-CaptureSessionFps expects an integer in [1, UINT_MAX].");
            }
            options.captureSessionEnabled = true;
        }
        else if (IsCommandLineArg(argv[i], L"-CaptureSessionMp4BitrateMbps"))
        {
            if (i + 1 >= argc || !TryParseUint(argv[++i], false, options.captureSessionMp4BitrateMbps) ||
                options.captureSessionMp4BitrateMbps > 100)
            {
                throw std::invalid_argument("-CaptureSessionMp4BitrateMbps expects an integer in [1, 100].");
            }
            options.captureSessionEnabled = true;
        }
        else if (IsCommandLineArg(argv[i], L"-CaptureSessionFrames"))
        {
            if (i + 1 >= argc || !TryParseUint(argv[++i], false, options.captureSessionFrameLimit))
            {
                throw std::invalid_argument("-CaptureSessionFrames expects an integer in [1, UINT_MAX].");
            }
            options.captureSessionEnabled = true;
        }
        else if (IsCommandLineArg(argv[i], L"-CaptureSessionWarmupFrames"))
        {
            if (i + 1 >= argc || !TryParseUint(argv[++i], true, options.captureSessionWarmupFrames))
            {
                throw std::invalid_argument("-CaptureSessionWarmupFrames expects an integer in [0, UINT_MAX].");
            }
            options.captureSessionEnabled = true;
        }
        else if (IsCommandLineArg(argv[i], L"-CaptureSessionEncoderDelayMs"))
        {
            if (i + 1 >= argc || !TryParseUint(argv[++i], true, options.captureSessionEncoderDelayMilliseconds) ||
                options.captureSessionEncoderDelayMilliseconds > 1000)
                throw std::invalid_argument("-CaptureSessionEncoderDelayMs expects an integer in [0, 1000].");
            options.captureSessionEnabled = true;
        }
        else if (IsCommandLineArg(argv[i], L"-CaptureSessionOrbitDegreesPerSecond"))
        {
            if (i + 1 >= argc)
                throw std::invalid_argument("-CaptureSessionOrbitDegreesPerSecond expects a value in [-360, 360].");
            wchar_t* end = nullptr;
            errno = 0;
            const WCHAR* value = argv[++i];
            const double rate = wcstod(value, &end);
            if (errno == ERANGE || end == value || *end != L'\0' || !std::isfinite(rate) || std::abs(rate) > 360.0)
                throw std::invalid_argument(
                    "-CaptureSessionOrbitDegreesPerSecond expects a finite value in [-360, 360].");
            options.captureSessionEnabled = true;
            options.hasCaptureSessionOrbit = true;
            options.captureSessionOrbitDegreesPerSecond = rate;
        }
        else if (IsCommandLineArg(argv[i], L"-CaptureSessionClock"))
        {
            if (i + 1 >= argc ||
                (_wcsicmp(argv[i + 1], L"real-time") != 0 && _wcsicmp(argv[i + 1], L"fixed-step") != 0))
            {
                throw std::invalid_argument("-CaptureSessionClock expects real-time or fixed-step.");
            }
            options.captureSessionEnabled = true;
            options.captureSessionFixedStep = _wcsicmp(argv[++i], L"fixed-step") == 0;
        }
        else if (IsCommandLineArg(argv[i], L"-CaptureSessionRoi"))
        {
            if (i + 4 >= argc || !TryParseUint(argv[++i], true, options.captureSessionRoiX) ||
                !TryParseUint(argv[++i], true, options.captureSessionRoiY) ||
                !TryParseUint(argv[++i], false, options.captureSessionRoiWidth) ||
                !TryParseUint(argv[++i], false, options.captureSessionRoiHeight))
            {
                throw std::invalid_argument("-CaptureSessionRoi expects x y width height with non-zero dimensions.");
            }
            options.captureSessionEnabled = true;
            options.hasCaptureSessionRoi = true;
        }
        else if (IsCommandLineArg(argv[i], L"-CaptureSessionDurationSeconds"))
        {
            if (i + 1 >= argc || !TryParsePositiveDouble(argv[++i], options.captureSessionDurationSeconds))
            {
                throw std::invalid_argument("-CaptureSessionDurationSeconds expects a finite value greater than zero.");
            }
            options.captureSessionEnabled = true;
            options.hasCaptureSessionDuration = true;
        }
        else if (IsCommandLineArg(argv[i], L"-ExitAfterCapture"))
        {
            options.exitAfterCapture = true;
        }
        else if (IsCommandLineArg(argv[i], L"-EnableDlssRayReconstruction"))
        {
            options.enableDlssRayReconstruction = true;
        }
        else if (IsCommandLineArg(argv[i], L"-EnableExperimentalNativeRayReconstruction"))
        {
            options.enableDlssRayReconstruction = true;
            options.enableExperimentalNativeRayReconstruction = true;
        }
        else if (IsCommandLineArg(argv[i], L"-CaptureReflectionResolvedRadiance"))
        {
            options.captureReflectionResolvedRadiance = true;
            options.captureReflectionTemporalValidity = false;
            options.reflectionCaptureDebugView = ReflectionCaptureDebugView::ResolvedRadiance;
        }
        else if (IsCommandLineArg(argv[i], L"-CaptureReflectionTemporalValidity"))
        {
            options.captureReflectionResolvedRadiance = true;
            options.captureReflectionTemporalValidity = true;
            options.reflectionCaptureDebugView = ReflectionCaptureDebugView::TemporalValidity;
        }
        else if (IsCommandLineArg(argv[i], L"-ReflectionCaptureDebugView"))
        {
            if (i + 1 < argc)
            {
                ReflectionCaptureDebugView debugView;
                if (TryParseReflectionCaptureDebugView(argv[++i], debugView))
                {
                    options.captureReflectionResolvedRadiance = true;
                    options.captureReflectionTemporalValidity =
                        debugView == ReflectionCaptureDebugView::TemporalValidity;
                    options.reflectionCaptureDebugView = debugView;
                }
            }
        }
        else if (IsCommandLineArg(argv[i], L"-ReflectionStochasticSampling"))
        {
            options.reflectionStochasticSampling = true;
        }
        else if (IsCommandLineArg(argv[i], L"-ReflectionEstimatorConstantIncidentRadiance"))
        {
            options.reflectionEstimatorConstantIncidentRadiance = true;
        }
        else if (IsCommandLineArg(argv[i], L"-ReflectionRejectedPixelNeighborhood"))
        {
            options.reflectionRejectedPixelNeighborhood = true;
        }
        else if (IsCommandLineArg(argv[i], L"-ReflectionSurfaceVarianceFilter"))
        {
            options.reflectionSurfaceVarianceFilter = true;
        }
        else if (IsCommandLineArg(argv[i], L"-ReflectionSpatiotemporalSpatialPolicy"))
        {
            options.reflectionSpatiotemporalSpatialPolicy = true;
        }
        else if (IsCommandLineArg(argv[i], L"-ReflectionVarianceGuidedTemporal"))
        {
            options.reflectionVarianceGuidedTemporal = true;
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
        else if (IsCommandLineArg(argv[i], L"-PathTracingCapturePlan"))
        {
            if (i + 1 >= argc || argv[i + 1][0] == L'-')
            {
                throw std::invalid_argument("-PathTracingCapturePlan expects a plan file.");
            }
            options.pathTracingCapturePlanPath = argv[++i];
            options.enablePathTracing = true;
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
        else if (IsCommandLineArg(argv[i], L"-ReflectionHdrDiagnostics"))
        {
            if (i + 1 < argc)
            {
                options.reflectionHdrDiagnosticsPath = argv[++i];
                options.captureReflectionResolvedRadiance = true;
            }
        }
        else if (IsCommandLineArg(argv[i], L"-ReflectionHdrDiagnosticsWarmupFrames"))
        {
            if (i + 1 < argc)
            {
                const int frames = _wtoi(argv[++i]);
                if (frames >= 0)
                {
                    options.reflectionHdrDiagnosticsWarmupFrames = static_cast<UINT>(frames);
                }
            }
        }
        else if (IsCommandLineArg(argv[i], L"-ReflectionHdrDiagnosticsFrames"))
        {
            if (i + 1 < argc)
            {
                const int frames = _wtoi(argv[++i]);
                if (frames > 0)
                {
                    options.reflectionHdrDiagnosticsFrames = static_cast<UINT>(frames);
                }
            }
        }
        else if (IsCommandLineArg(argv[i], L"-ReflectionHdrDiagnosticsRoi"))
        {
            if (i + 4 < argc)
            {
                const int x = _wtoi(argv[++i]);
                const int y = _wtoi(argv[++i]);
                const int width = _wtoi(argv[++i]);
                const int height = _wtoi(argv[++i]);
                if (x >= 0 && y >= 0 && width > 0 && height > 0)
                {
                    options.reflectionHdrDiagnosticsRoiX = static_cast<UINT>(x);
                    options.reflectionHdrDiagnosticsRoiY = static_cast<UINT>(y);
                    options.reflectionHdrDiagnosticsRoiWidth = static_cast<UINT>(width);
                    options.reflectionHdrDiagnosticsRoiHeight = static_cast<UINT>(height);
                }
            }
        }
        else if (IsCommandLineArg(argv[i], L"-ReflectionConfidenceForceStableAfterMeasurementFrames"))
        {
            if (i + 1 < argc)
            {
                const int frames = _wtoi(argv[++i]);
                if (frames > 0)
                {
                    options.reflectionConfidenceForceStableAfterMeasurementFrames =
                        static_cast<UINT>(frames);
                }
            }
        }
        else if (IsCommandLineArg(argv[i], L"-ReflectionHistoryResetAfterMeasurementFrames"))
        {
            if (i + 1 < argc)
            {
                const int frames = _wtoi(argv[++i]);
                if (frames > 0)
                {
                    options.reflectionHistoryResetAfterMeasurementFrames = static_cast<UINT>(frames);
                }
            }
        }
    }

    if (!options.reflectionHdrDiagnosticsPath.empty() &&
        !options.autoSelectGltfDamagedHelmet &&
        options.autoSelectGltfAssetName.empty() &&
        !options.autoSelectHybridReflectionEstimatorTest)
    {
        options.autoSelectGltfDamagedHelmet = true;
    }

    if (options.rrFenceTraceCalibrateVerifiedGpu &&
        (options.rrFenceTracePath.empty() || !options.rrFenceTraceTimingOnly || !options.pathTracingRrVerifiedPresent))
    {
        throw std::invalid_argument("-RrFenceTraceCalibrateVerifiedGpu requires a timing-only VerifiedPresent trace.");
    }

    if (options.rrFenceTraceVSyncCycle &&
        (options.rrFenceTracePath.empty() || options.rrFenceTraceTimingOnly || options.rrFenceTraceSeconds < 12 ||
         options.disableVSync))
    {
        throw std::invalid_argument("-RrFenceTraceVSyncCycle requires a detailed trace of at least 12 seconds "
                                    "starting with VSync enabled.");
    }

    if (options.rrFenceTraceNonblockingCycle &&
        (options.rrFenceTracePath.empty() || options.rrFenceTraceTimingOnly || options.rrFenceTraceSeconds < 12 ||
         options.disableVSync || options.rrFenceTraceVSyncCycle || options.nonblockingFrameAcquisition))
    {
        throw std::invalid_argument("-RrFenceTraceNonblockingCycle requires a detailed trace of at least 12 seconds "
                                    "with VSync enabled and blocking acquisition at startup.");
    }

    if (options.pathTracingRrSeparateCompletionFence && !options.pathTracingRrChained)
    {
        throw std::invalid_argument("-PathTracingRrSeparateCompletionFence requires -PathTracingRrSchedule chained.");
    }

    if (options.pathTracingRrVerifiedPresent &&
        (!options.pathTracingRrSeparateCompletionFence ||
         (!options.rrFenceTracePath.empty() && !options.rrFenceTraceTimingOnly) ||
         !options.pathTracingRrComparisonCaptureDirectory.empty() ||
         !options.pathTracingRrRegressionValidationDirectory.empty() ||
         !options.pathTracingRrRecoveryValidationDirectory.empty()))
    {
        throw std::invalid_argument("-PathTracingRrVerifiedPresent requires the separate RR completion fence and "
                                    "supports timing-only traces and regular scene rendering.");
    }

    return options;
}

bool BuildCaptureSessionConfig(const CommandLineOptions& options,
                               RtPbrSurvey::CaptureSessionConfig& config,
                               std::string& error)
{
    error.clear();
    if (!options.captureSessionEnabled)
    {
        error = "Capture Session was not requested.";
        return false;
    }
    if (options.captureSessionOutputDirectory.empty() || options.captureSessionBaseName.empty())
    {
        error = "Capture Session requires -CaptureSessionOutputDir and -CaptureSessionBaseName.";
        return false;
    }
    if (options.hasCaptureSessionOrbit &&
        (!std::isfinite(options.captureSessionOrbitDegreesPerSecond) ||
         std::abs(options.captureSessionOrbitDegreesPerSecond) > 360.0 || !options.captureSessionFixedStep ||
         !options.capturePath.empty() || !options.reflectionCapturePlanPath.empty() ||
         !options.reflectionHdrDiagnosticsPath.empty() || options.reflectionOrbitFrames > 0 ||
         !options.pathTracingHistoryValidationDirectory.empty()))
    {
        error = "Capture Session orbit requires fixed-step clock and no other camera/capture automation.";
        return false;
    }
    if (options.captureSessionFrameLimit == 0 && !options.hasCaptureSessionDuration)
    {
        error = "Capture Session requires -CaptureSessionFrames or -CaptureSessionDurationSeconds.";
        return false;
    }

    RtPbrSurvey::CaptureSessionOutputFormat format;
    if (!TryParseCaptureSessionFormat(options.captureSessionFormat, format))
    {
        error = "-CaptureSessionFormat expects png, exr, gif, or mp4.";
        return false;
    }
    if (options.captureSessionEncoderDelayMilliseconds > 0 && format != RtPbrSurvey::CaptureSessionOutputFormat::Mp4)
    {
        error = "Capture Session encoder delay is a diagnostic option for MP4 only.";
        return false;
    }

    RtPbrSurvey::CaptureSessionGifRepeatMode gifRepeatMode;
    std::uint16_t gifRepeatCount = 0;
    if (!TryParseCaptureSessionGifRepeat(options.captureSessionGifRepeat, gifRepeatMode, gifRepeatCount))
    {
        error = "-CaptureSessionGifRepeat expects none, infinite, or a repeat count in [1, 65535].";
        return false;
    }
    RtPbrSurvey::CaptureSessionGifDisposal gifDisposal;
    if (!TryParseCaptureSessionGifDisposal(options.captureSessionGifDisposal, gifDisposal))
    {
        error = "-CaptureSessionGifDisposal expects keep, background, or previous.";
        return false;
    }

    config = {};
    config.outputDirectory = options.captureSessionOutputDirectory;
    config.outputSubdirectory = options.captureSessionOutputSubdirectory;
    config.baseName = WideToUtf8(options.captureSessionBaseName);
    config.outputFormat = format;
    config.gifRepeatMode = gifRepeatMode;
    config.gifRepeatCount = gifRepeatCount;
    config.gifDisposal = gifDisposal;
    config.source = format == RtPbrSurvey::CaptureSessionOutputFormat::Exr ?
        RtPbrSurvey::ScreenshotCaptureSource::PreToneMapSceneColor : RtPbrSurvey::ScreenshotCaptureSource::FinalOutput;
    config.clock = options.captureSessionFixedStep ?
        RtPbrSurvey::CaptureSessionClock::FixedStep : RtPbrSurvey::CaptureSessionClock::RealTime;
    config.framesPerSecond = options.captureSessionFramesPerSecond;
    config.mp4Bitrate = options.captureSessionMp4BitrateMbps * 1000000;
    config.warmupFrames = options.captureSessionWarmupFrames;
    if (options.captureSessionFrameLimit > 0)
    {
        config.frameLimit = options.captureSessionFrameLimit;
    }
    if (options.hasCaptureSessionDuration)
    {
        config.durationSeconds = options.captureSessionDurationSeconds;
    }
    if (options.hasCaptureSessionRoi)
    {
        config.region = RtPbrSurvey::ScreenshotRegion{
            options.captureSessionRoiX,
            options.captureSessionRoiY,
            options.captureSessionRoiWidth,
            options.captureSessionRoiHeight,
        };
    }
    return true;
}

bool HasPathTracingDlssConflict(const CommandLineOptions& options)
{
    return options.enablePathTracing &&
           (options.enableDlssSr || options.captureReflectionResolvedRadiance ||
            (options.enableDlssRayReconstruction && options.pathTracingReconstruction != "rr"));
}

static bool LoadCapturePlan(const std::filesystem::path& path,
                            const std::string& variant,
                            const wchar_t* requiredExtension,
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
                                  requiredExtension,
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

bool LoadReflectionCapturePlan(const std::filesystem::path& path,
                               const std::string& variant,
                               ReflectionCapturePlan& plan,
                               std::string& error)
{
    return LoadCapturePlan(path, variant, L".png", plan, error);
}

bool LoadPathTracingCapturePlan(const std::filesystem::path& path, ReflectionCapturePlan& plan, std::string& error)
{
    if (!LoadCapturePlan(path, "pt", L".ptbuf", plan, error))
    {
        return false;
    }
    std::vector<std::filesystem::path> paths;
    for (const ReflectionCaptureRequest& capture : plan.captures)
    {
        const std::filesystem::path normalized = capture.path.lexically_normal();
        if (std::any_of(paths.begin(), paths.end(), [&](const std::filesystem::path& previous)
                        { return _wcsicmp(previous.c_str(), normalized.c_str()) == 0; }))
        {
            error = "Path Tracing capture paths must be unique.";
            plan = {};
            return false;
        }
        paths.push_back(normalized);
    }
    return true;
}

} // namespace Platform
