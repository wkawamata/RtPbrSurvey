#include "stdafx.h"

#include "Platform/AssetPath.h"
#include "Renderer/EnvironmentMap.h"

#include <iostream>

int main()
{
    const std::wstring executableDirectory = Platform::GetApplicationDirectoryPath();
    const std::wstring runtimeAssetsPath = Platform::GetRuntimeAssetsPath();
    const std::wstring assetPath = Platform::GetAssetFullPath(L"Environment\\default_environment.hdr");

    if (runtimeAssetsPath != executableDirectory + L"Assets\\")
    {
        std::cerr << "Runtime assets path is not relative to the executable directory.\n";
        return 1;
    }

    if (assetPath != executableDirectory + L"Assets\\Environment\\default_environment.hdr")
    {
        std::cerr << "Asset path does not resolve below the executable runtime assets directory.\n";
        return 1;
    }

    Engine::HdrImage hdrImage;
    if (!Engine::TryLoadHdrImage(assetPath.c_str(), hdrImage))
    {
        std::wcerr << L"Failed to load HDRI: " << assetPath << L"\n";
        return 1;
    }

    std::wcout << L"Loaded HDRI: " << assetPath << L" (" << hdrImage.width << L" x " << hdrImage.height << L")\n";
    return 0;
}
