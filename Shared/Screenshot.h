#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace RtPbrSurvey
{
struct ScreenshotRegion
{
    std::uint32_t x = 0;
    std::uint32_t y = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

enum class ScreenshotOutputFormat
{
    Png,
    Exr,
    Gif,
};

enum class ScreenshotCaptureSource
{
    // The composed display output, including ImGui.
    FinalOutput,

    // Linear scene color before tone mapping. This excludes ImGui.
    PreToneMapSceneColor,
};

struct ScreenshotRequest
{
    std::filesystem::path path;
    std::optional<ScreenshotRegion> region;
    ScreenshotOutputFormat outputFormat = ScreenshotOutputFormat::Png;
    ScreenshotCaptureSource source = ScreenshotCaptureSource::FinalOutput;
    // GIF uses centiseconds because that is the timing resolution of its frame metadata.
    std::uint16_t frameDelayCentiseconds = 0;
    // GIF loop extension: unset plays once, zero loops forever, positive values repeat after the first play.
    std::optional<std::uint16_t> gifRepeatCount;
    // GIF graphic-control disposal: 1 keeps the composed frame, 2 restores the background, 3 restores the previous frame.
    std::uint8_t gifDisposal = 1;
    std::uint64_t requestId = 0;
};

struct ScreenshotResult
{
    std::filesystem::path path;
    bool succeeded = false;
    std::string error;
    unsigned int width = 0;
    unsigned int height = 0;
    std::uint64_t requestId = 0;
};
} // namespace RtPbrSurvey
