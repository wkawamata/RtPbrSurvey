#pragma once

#include <Windows.h>

#include <string>

namespace Platform
{

inline void SetWindowTitle(HWND hwnd, const std::wstring& baseTitle, LPCWSTR customText)
{
    std::wstring windowText = baseTitle + L": " + customText;
    SetWindowText(hwnd, windowText.c_str());
}

} // namespace Platform
