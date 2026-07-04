//*********************************************************
//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
//*********************************************************

#include "stdafx.h"
#include "DXSample.h"
#include "Platform/FileIO.h"
#include "Win32Application.h"

DXSample::DXSample(UINT width, UINT height, std::wstring name)
    : m_windowInfo(Platform::CreateWindowInfo(width, height, name))
{
    WCHAR assetsPath[512];
    GetAssetsPath(assetsPath, _countof(assetsPath));
    m_assetsPath = assetsPath;
}

DXSample::~DXSample() {}

// Helper function for resolving the full path of assets.
std::wstring DXSample::GetAssetFullPath(LPCWSTR assetName)
{
    return m_assetsPath + assetName;
}

// Helper function for setting the window's title text.
void DXSample::SetCustomWindowText(LPCWSTR text)
{
    std::wstring windowText = m_windowInfo.title + L": " + text;
    SetWindowText(Win32Application::GetHwnd(), windowText.c_str());
}

// Helper function for parsing any supplied command line args.
_Use_decl_annotations_ void DXSample::ParseCommandLineArgs(WCHAR* argv[], int argc)
{
    m_commandLineOptions = Platform::ParseCommandLineOptions(argv, argc);
    if (m_commandLineOptions.useWarpDevice)
    {
        m_windowInfo.title = m_windowInfo.title + L" (WARP)";
    }
}
