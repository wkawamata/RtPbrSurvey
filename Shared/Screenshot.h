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

struct ScreenshotRequest
{
    std::filesystem::path path;
    std::optional<ScreenshotRegion> region;
};

struct ScreenshotResult
{
    std::filesystem::path path;
    bool succeeded = false;
    std::string error;
    unsigned int width = 0;
    unsigned int height = 0;
};
} // namespace RtPbrSurvey
