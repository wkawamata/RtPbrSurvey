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

#pragma once

#include "Platform/CommandLineOptions.h"
#include "Platform/IApplication.h"
#include "Platform/WindowInfo.h"

#include <string>

class DXSample : public Platform::IApplication
{
public:
    DXSample(UINT width, UINT height, std::wstring name);
    virtual ~DXSample();

    // IApplication overrides.
    void OnInit() override = 0;
    void OnDestroy() override = 0;

    void OnKeyDown(UINT8 /*key*/) override {}
    void OnKeyUp(UINT8 /*key*/) override {}
    void OnMouseDown(UINT8 /*button*/, int /*x*/, int /*y*/) override {}
    void OnMouseUp(UINT8 /*button*/, int /*x*/, int /*y*/) override {}
    void OnMouseMove(int /*x*/, int /*y*/) override {}
    void OnMouseWheel(int /*wheelDelta*/) override {}
    void OnWindowSizeChanged(UINT, UINT) override {}
    void OnIdle() override {}

    UINT GetWidth() const override
    {
        return m_windowInfo.width;
    }
    UINT GetHeight() const override
    {
        return m_windowInfo.height;
    }
    const WCHAR* GetTitle() const override
    {
        return m_windowInfo.title.c_str();
    }

    void ParseCommandLineArgs(_In_reads_(argc) WCHAR* argv[], int argc) override;

protected:
    Platform::WindowInfo m_windowInfo;

    // Adapter info.
    Platform::CommandLineOptions m_commandLineOptions;

private:
};
