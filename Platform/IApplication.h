#pragma once

#include <Windows.h>

#include <string>

namespace Platform
{

struct IApplication
{
    virtual ~IApplication() = default;

    // Called by application host (Win32Application) before window creation.
    virtual void ParseCommandLineArgs(WCHAR* argv[], int argc) = 0;

    // Lifecycle.
    virtual void OnInit() = 0;
    virtual void OnDestroy() = 0;

    // Per-frame callback. The host calls this each iteration of the message loop.
    virtual void OnIdle() {}

    // Window queries used during host window creation.
    virtual UINT GetWidth() const = 0;
    virtual UINT GetHeight() const = 0;
    virtual const WCHAR* GetTitle() const = 0;

    // Input event callbacks from the host's window procedure.
    virtual void OnKeyDown(UINT8 key) {}
    virtual void OnKeyUp(UINT8 key) {}
    virtual void OnMouseDown(UINT8 button, int x, int y) {}
    virtual void OnMouseUp(UINT8 button, int x, int y) {}
    virtual void OnMouseMove(int x, int y) {}
    virtual void OnMouseWheel(int wheelDelta) {}
    virtual void OnWindowSizeChanged(UINT width, UINT height) {}
};

} // namespace Platform
