#pragma once

#include <Windows.h>

#include <string>
#include <utility>

namespace Platform
{

struct WindowInfo
{
    UINT width = 0;
    UINT height = 0;
    float aspectRatio = 1.0f;
    std::wstring title;
};

inline WindowInfo CreateWindowInfo(UINT width, UINT height, std::wstring title)
{
    WindowInfo info = {};
    info.width = width;
    info.height = height;
    info.aspectRatio = static_cast<float>(width) / static_cast<float>(height);
    info.title = std::move(title);
    return info;
}

} // namespace Platform
