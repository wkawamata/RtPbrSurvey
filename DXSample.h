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
#include "Platform/WindowInfo.h"

#include <string>

class DXSample
{
public:
    DXSample(UINT width, UINT height, std::wstring name);
    virtual ~DXSample();

    virtual void OnInit() = 0;
    virtual void OnDestroy() = 0;

    // Samples override the event handlers to handle specific messages.
    virtual void OnKeyDown(UINT8 /*key*/) {}
    virtual void OnKeyUp(UINT8 /*key*/) {}
    virtual void OnMouseDown(UINT8 /*button*/, int /*x*/, int /*y*/) {}
    virtual void OnMouseUp(UINT8 /*button*/, int /*x*/, int /*y*/) {}
    virtual void OnMouseMove(int /*x*/, int /*y*/) {}
    virtual void OnMouseWheel(int /*wheelDelta*/) {}
    virtual void OnWindowSizeChanged(UINT, UINT) {}
    virtual void OnIdle() {}

    // Accessors.
    UINT GetWidth() const
    {
        return m_windowInfo.width;
    }
    UINT GetHeight() const
    {
        return m_windowInfo.height;
    }
    const WCHAR* GetTitle() const
    {
        return m_windowInfo.title.c_str();
    }

    void ParseCommandLineArgs(_In_reads_(argc) WCHAR* argv[], int argc);

protected:
    Platform::WindowInfo m_windowInfo;

    // Adapter info.
    Platform::CommandLineOptions m_commandLineOptions;

private:
};
