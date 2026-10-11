#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

#include <wincodec.h>
#include <wrl/client.h>

namespace Engine
{
    class AnimatedGifEncoder
    {
    public:
        ~AnimatedGifEncoder();
        bool AppendFrame(const std::filesystem::path& path,
                         unsigned int width,
                         unsigned int height,
                         const std::uint8_t* rgba8,
                         std::uint16_t delayCentiseconds,
                         std::optional<std::uint16_t> repeatCount,
                         std::uint8_t disposal,
                         std::string& error);
        bool Finalize(std::string& error);
        void Reset();
        bool IsActive() const;

    private:
        bool Begin(const std::filesystem::path& path,
                   unsigned int width,
                   unsigned int height,
                   std::optional<std::uint16_t> repeatCount,
                   std::string& error);

        Microsoft::WRL::ComPtr<IWICImagingFactory> m_factory;
        Microsoft::WRL::ComPtr<IWICStream> m_stream;
        Microsoft::WRL::ComPtr<IWICBitmapEncoder> m_encoder;
        unsigned int m_width = 0;
        unsigned int m_height = 0;
        bool m_ownsComInitialization = false;
    };
} // namespace Engine
