#pragma once

#include "Platform/FileIO.h"

#include <string>

namespace Platform
{

inline std::wstring GetApplicationDirectoryPath()
{
    WCHAR path[512];
    GetAssetsPath(path, _countof(path));
    return path;
}

inline std::wstring GetApplicationAssetsPath()
{
    return GetApplicationDirectoryPath();
}

inline std::wstring GetRuntimeAssetsPath()
{
    return GetApplicationDirectoryPath() + L"Assets\\";
}

inline std::wstring GetAssetFullPath(LPCWSTR assetName)
{
    return GetRuntimeAssetsPath() + assetName;
}

} // namespace Platform
