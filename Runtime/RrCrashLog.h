#pragma once

#include <Windows.h>
#include <cstdint>
#include <cstdio>
#include <cwchar>
#include <filesystem>
#include <system_error>
#include <string>

namespace RtPbrSurvey
{
// Opt-in lifecycle diagnostics. All hooks run on the application thread.
class RrCrashLog
{
public:
    RrCrashLog() = default;
    ~RrCrashLog() { Close(); }
    RrCrashLog(const RrCrashLog&) = delete;
    RrCrashLog& operator=(const RrCrashLog&) = delete;

    void Open(const std::filesystem::path& path)
    {
        Close();
        std::filesystem::path logPath = path;
        unsigned int suffix = 0;
        for (;;)
        {
            m_file = CreateFileW(logPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                                 CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (m_file != INVALID_HANDLE_VALUE)
                break;

            const DWORD error = GetLastError();
            if (error != ERROR_FILE_EXISTS && error != ERROR_ALREADY_EXISTS)
            {
                throw std::system_error(static_cast<int>(error), std::system_category(),
                                        "Cannot create RR crash log");
            }
            if (++suffix > 1000)
            {
                Warn(L"RR crash log path and all numbered alternatives already exist. Logging disabled.\n");
                return;
            }
            logPath = path.parent_path() / (path.stem().wstring() + L"-" + std::to_wstring(suffix) +
                                             path.extension().wstring());
        }
        m_sequence = 0;
        if (!Write("log.start", GetCurrentProcessId(), 1, 0))
        {
            const DWORD error = GetLastError();
            Close();
            throw std::system_error(static_cast<int>(error), std::system_category(),
                                    "Cannot persist RR crash log header");
        }
        if (suffix != 0)
        {
            Warn(L"RR crash log already exists. Writing to: " + logPath.wstring() + L"\n");
            Record("log.path.collision", suffix);
        }
    }

    void Record(const char* event, uint64_t a = 0, uint64_t b = 0, uint64_t c = 0) noexcept
    {
        if (m_file != INVALID_HANDLE_VALUE && !Write(event, a, b, c))
        {
            OutputDebugStringA("RR crash log write/flush failed. Logging disabled.\n");
            Close();
        }
    }

    bool IsOpen() const noexcept { return m_file != INVALID_HANDLE_VALUE; }

    void Close() noexcept
    {
        if (m_file != INVALID_HANDLE_VALUE)
        {
            CloseHandle(m_file);
            m_file = INVALID_HANDLE_VALUE;
        }
    }

private:
    static void Warn(const std::wstring& message)
    {
        OutputDebugStringW(message.c_str());
        std::fwprintf(stderr, L"%ls", message.c_str());
    }

    bool Write(const char* event, uint64_t a, uint64_t b, uint64_t c) noexcept
    {
        SYSTEMTIME utc = {};
        GetSystemTime(&utc);
        char line[256];
        const int length = std::snprintf(
            line, sizeof(line), "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ %llu %s %llu %llu %llu\r\n",
            unsigned(utc.wYear), unsigned(utc.wMonth), unsigned(utc.wDay), unsigned(utc.wHour),
            unsigned(utc.wMinute), unsigned(utc.wSecond), unsigned(utc.wMilliseconds),
            static_cast<unsigned long long>(++m_sequence), event, static_cast<unsigned long long>(a),
            static_cast<unsigned long long>(b), static_cast<unsigned long long>(c));
        if (length <= 0 || length >= static_cast<int>(sizeof(line)))
        {
            SetLastError(ERROR_INSUFFICIENT_BUFFER);
            return false;
        }
        const DWORD size = static_cast<DWORD>(length);
        DWORD written = 0;
        if (!WriteFile(m_file, line, size, &written, nullptr))
        {
            return false;
        }
        if (written != size)
        {
            SetLastError(ERROR_WRITE_FAULT);
            return false;
        }
        return FlushFileBuffers(m_file) != FALSE;
    }

    HANDLE m_file = INVALID_HANDLE_VALUE;
    uint64_t m_sequence = 0;
};

inline RrCrashLog g_rrCrashLog;
} // namespace RtPbrSurvey
