#pragma once

#include "Runtime/CaptureSession.h"

#include <string>

namespace RtPbrSurvey
{
    class SceneRenderer;

    enum class CaptureSessionUiAction
    {
        None,
        Start,
        Stop,
    };

    struct CaptureSessionUiState
    {
        std::string outputDirectory = "Screenshots";
        std::string baseName = "capture";
        int outputFormat = static_cast<int>(CaptureSessionOutputFormat::Png);
        bool useRegion = false;
        int regionX = 0;
        int regionY = 0;
        int regionWidth = 640;
        int regionHeight = 480;
        int framesPerSecond = 60;
        int warmupFrames = 0;
        bool useFrameLimit = true;
        int frameLimit = 60;
        bool useDurationLimit = false;
        float durationSeconds = 1.0f;
        bool fixedStep = false;
        std::string message;
    };

    class CaptureSessionUi
    {
    public:
        static CaptureSessionConfig BuildConfig(const CaptureSessionUiState& state);
        static bool IsActive(const CaptureSessionStatus& status);
        static void Update(SceneRenderer& renderer, const CaptureSessionTiming& timing);
        static void Draw(SceneRenderer& renderer, CaptureSessionUiState& state);
        static CaptureSessionUiAction Draw(const CaptureSessionStatus& status, CaptureSessionUiState& state);
    };
} // namespace RtPbrSurvey
