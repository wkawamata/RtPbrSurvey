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
    std::uint64_t requestId = 0;
    // Used only by diagnostic .ptbuf captures.
    std::string debugResourceName;
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
