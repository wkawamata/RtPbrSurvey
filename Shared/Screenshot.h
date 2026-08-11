#pragma once

#include <filesystem>
#include <string>

namespace RtPbrSurvey
{
struct ScreenshotRequest
{
    std::filesystem::path path;
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
