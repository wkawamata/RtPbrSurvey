#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>

namespace Engine
{
    // Incremental H.264/MP4 writer. All calls and destruction belong to one thread.
    class Mp4Encoder
    {
    public:
        Mp4Encoder() = default;
        Mp4Encoder(const Mp4Encoder&) = delete;
        Mp4Encoder& operator=(const Mp4Encoder&) = delete;
        ~Mp4Encoder();
        bool AppendFrame(const std::filesystem::path& path, unsigned int width, unsigned int height,
                         const std::uint8_t* rgba8, std::uint32_t framesPerSecond,
                         std::uint32_t bitrate, std::string& error,
                         std::optional<std::uint64_t> timestamp100ns = std::nullopt);
        bool Finalize(std::string& error, std::optional<std::uint64_t> endTimestamp100ns = std::nullopt);
        void Reset();

    private:
        bool Begin(const std::filesystem::path& path, unsigned int width, unsigned int height,
                   std::uint32_t framesPerSecond, std::uint32_t bitrate, std::string& error);
        HRESULT WritePendingFrame(LONGLONG endTime);

        Microsoft::WRL::ComPtr<IMFSinkWriter> m_writer;
        Microsoft::WRL::ComPtr<IMFByteStream> m_stream;
        Microsoft::WRL::ComPtr<IMFSample> m_pendingSample;
        LONGLONG m_pendingTime = 0;
        std::filesystem::path m_path;
        unsigned int m_width = 0;
        unsigned int m_height = 0;
        unsigned int m_encodedWidth = 0;
        unsigned int m_encodedHeight = 0;
        std::uint32_t m_fps = 0;
        std::uint32_t m_bitrate = 0;
        std::uint64_t m_frameIndex = 0;
        DWORD m_streamIndex = 0;
        bool m_ownsCom = false;
        bool m_ownsMediaFoundation = false;
        bool m_removeIncomplete = false;
    };
} // namespace Engine
