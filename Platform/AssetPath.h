#pragma once

#include "Platform/FileIO.h"

#include <string>

namespace Platform
{

inline std::wstring GetApplicationAssetsPath()
{
    WCHAR path[512];
    GetAssetsPath(path, _countof(path));
    return path;
}

inline std::wstring GetAssetFullPath(LPCWSTR assetName)
{
    return GetApplicationAssetsPath() + assetName;
}

} // namespace Platform
