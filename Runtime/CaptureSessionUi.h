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
        std::string outputSubdirectory;
        std::string baseName = "capture";
        int outputFormat = static_cast<int>(CaptureSessionOutputFormat::Png);
        bool useRegion = false;
        bool showRegionOverlay = true;
        bool selectingRegion = false;
        bool draggingRegion = false;
        float regionDragStartX = 0.0f;
        float regionDragStartY = 0.0f;
        int regionX = 0;
        int regionY = 0;
        int regionWidth = 640;
        int regionHeight = 480;
        int framesPerSecond = 60;
        int gifRepeatMode = static_cast<int>(CaptureSessionGifRepeatMode::Infinite);
        int gifRepeatCount = 1;
        int mp4BitrateMbps = 12;
        int gifDisposal = static_cast<int>(CaptureSessionGifDisposal::Keep);
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
        // Draw once at the end of the host's UI frame, even when the settings panel is hidden.
        static void DrawRegionOverlay(const CaptureSessionStatus& status, CaptureSessionUiState& state,
                                      std::uint32_t outputWidth, std::uint32_t outputHeight);
        static void CancelRegionSelection(CaptureSessionUiState& state);
        static std::optional<ScreenshotRegion> RegionFromDrag(float startX, float startY, float endX, float endY,
                                                              float displayWidth, float displayHeight,
                                                              std::uint32_t outputWidth, std::uint32_t outputHeight);
    };
} // namespace RtPbrSurvey
