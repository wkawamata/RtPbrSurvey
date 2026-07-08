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
#include "Win32Application.h"

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "MyDx12Utils.h"
#include <windowsx.h>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

HWND Win32Application::m_hwnd = nullptr;

int Win32Application::Run(Platform::IApplication* pApp, HINSTANCE hInstance, int nCmdShow)
{
    // Parse the command line parameters
    int argc;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    pApp->ParseCommandLineArgs(argv, argc);
    LocalFree(argv);

    // Initialize the window class.
    WNDCLASSEX windowClass = {0};
    windowClass.cbSize = sizeof(WNDCLASSEX);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = hInstance;
    windowClass.hCursor = LoadCursor(NULL, IDC_ARROW);
    windowClass.lpszClassName = L"RtPbrSurveyAppClass";
    RegisterClassEx(&windowClass);

    RECT windowRect = {0, 0, static_cast<LONG>(pApp->GetWidth()), static_cast<LONG>(pApp->GetHeight())};
    AdjustWindowRect(&windowRect, WS_OVERLAPPEDWINDOW, FALSE);

    // Create the window and store a handle to it.
    m_hwnd = CreateWindow(windowClass.lpszClassName,
                          pApp->GetTitle(),
                          WS_OVERLAPPEDWINDOW,
                          CW_USEDEFAULT,
                          CW_USEDEFAULT,
                          windowRect.right - windowRect.left,
                          windowRect.bottom - windowRect.top,
                          nullptr, // We have no parent window.
                          nullptr, // We aren't using menus.
                          hInstance,
                          pApp);

    // Initialize the application. OnInit is defined in each child-implementation of IApplication.
    pApp->OnInit();

    ShowWindow(m_hwnd, nCmdShow);

    // Main sample loop.
    MSG msg = {};
    bool shouldQuit = false;
    while (!shouldQuit)
    {
        // Process any messages in the queue.
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT)
            {
                shouldQuit = true;
                break;
            }
            // DBG_PRINT("msg = %u\n", msg.message);
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        if (shouldQuit)
        {
            break;
        }

        // DBG_PRINT("idle\n", msg.message);
        pApp->OnIdle();
    }

    pApp->OnDestroy();

    // Return this part of the WM_QUIT message to Windows.
    return static_cast<char>(msg.wParam);
}

// Main message handler for the sample.
LRESULT CALLBACK Win32Application::WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hWnd, message, wParam, lParam))
        return true;

    Platform::IApplication* pApp = reinterpret_cast<Platform::IApplication*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));

    switch (message)
    {
        case WM_CREATE:
        {
            // Save the IApplication* passed in to CreateWindow.
            LPCREATESTRUCT pCreateStruct = reinterpret_cast<LPCREATESTRUCT>(lParam);
            SetWindowLongPtr(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pCreateStruct->lpCreateParams));
        }
            return 0;

        case WM_SIZE:
            if (pApp)
            {
                UINT width = LOWORD(lParam);
                UINT height = HIWORD(lParam);

                if (wParam != SIZE_MINIMIZED)
                {
                    pApp->OnWindowSizeChanged(width, height);
                }
            }
            return 0;

        case WM_KEYDOWN:
            if (pApp)
            {
                const bool wasKeyDown = (lParam & (1 << 30)) != 0;
                if (!wasKeyDown)
                {
                    pApp->OnKeyDown(static_cast<UINT8>(wParam));
                }
            }
            return 0;

        case WM_KEYUP:
            if (pApp)
            {
                pApp->OnKeyUp(static_cast<UINT8>(wParam));
            }
            return 0;

        case WM_LBUTTONDOWN:
            if (pApp)
            {
                if (ImGui::GetIO().WantCaptureMouse)
                {
                    return 0;
                }
                SetCapture(hWnd);
                pApp->OnMouseDown(VK_LBUTTON, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            }
            return 0;

        case WM_LBUTTONUP:
            if (pApp)
            {
                ReleaseCapture();
                pApp->OnMouseUp(VK_LBUTTON, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            }
            return 0;

        case WM_MBUTTONDOWN:
            if (pApp)
            {
                if (ImGui::GetIO().WantCaptureMouse)
                {
                    return 0;
                }
                SetCapture(hWnd);
                pApp->OnMouseDown(VK_MBUTTON, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            }
            return 0;

        case WM_MBUTTONUP:
            if (pApp)
            {
                ReleaseCapture();
                pApp->OnMouseUp(VK_MBUTTON, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            }
            return 0;

        case WM_MOUSEMOVE:
            if (pApp)
            {
                pApp->OnMouseMove(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            }
            return 0;

        case WM_MOUSEWHEEL:
            if (pApp)
            {
                if (ImGui::GetIO().WantCaptureMouse)
                {
                    return 0;
                }
                pApp->OnMouseWheel(GET_WHEEL_DELTA_WPARAM(wParam));
            }
            return 0;

        case WM_PAINT:
            if (pApp)
            {
                PAINTSTRUCT ps;
                BeginPaint(hWnd, &ps);
                EndPaint(hWnd, &ps);
                return 0;
            }
            return 0;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }

    // Handle any messages the switch statement didn't.
    return DefWindowProc(hWnd, message, wParam, lParam);
}
