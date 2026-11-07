#pragma once

#include <windows.h>
#include <shellapi.h>
#include <filesystem>

#pragma comment(lib, "Shell32.lib")

namespace RtPbrSurvey
{
inline bool OpenOutputDirectory(const std::filesystem::path& directory)
{
    std::error_code error;
    const auto path = std::filesystem::absolute(directory, error);
    if (directory.empty() || error || !std::filesystem::is_directory(path, error))
    {
        return false;
    }
    return reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) > 32;
}
}
