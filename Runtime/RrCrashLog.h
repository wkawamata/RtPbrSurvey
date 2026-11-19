#pragma once

#include <Windows.h>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <system_error>

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
        m_file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                             CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (m_file == INVALID_HANDLE_VALUE)
        {
            throw std::system_error(static_cast<int>(GetLastError()), std::system_category(),
                                    "Cannot create RR crash log");
        }
        m_sequence = 0;
        if (!Write("log.start", GetCurrentProcessId(), 1, 0))
        {
            const DWORD error = GetLastError();
            Close();
            throw std::system_error(static_cast<int>(error), std::system_category(),
                                    "Cannot persist RR crash log header");
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

    void Close() noexcept
    {
        if (m_file != INVALID_HANDLE_VALUE)
        {
            CloseHandle(m_file);
            m_file = INVALID_HANDLE_VALUE;
        }
    }

private:
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
