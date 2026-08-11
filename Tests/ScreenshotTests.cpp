#include "stdafx.h"

#include "Renderer/ScreenshotCapture.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

namespace
{
bool Check(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "FAILED: " << message << '\n';
    }
    return condition;
}

std::uint32_t PackR10G10B10A2(std::uint32_t r, std::uint32_t g, std::uint32_t b, std::uint32_t a)
{
    return (r & 0x3ff) | ((g & 0x3ff) << 10) | ((b & 0x3ff) << 20) | ((a & 0x3) << 30);
}

std::uint32_t ReadBigEndian32(const std::uint8_t* data)
{
    return (static_cast<std::uint32_t>(data[0]) << 24) | (static_cast<std::uint32_t>(data[1]) << 16) |
           (static_cast<std::uint32_t>(data[2]) << 8) | static_cast<std::uint32_t>(data[3]);
}

bool TestSdrConversion()
{
    const std::array<std::uint32_t, 2> source = {
        PackR10G10B10A2(1023, 0, 512, 3),
        PackR10G10B10A2(0, 1023, 0, 0),
    };
    const std::vector<std::uint8_t> converted =
        Engine::ConvertScreenshotToRgba8(reinterpret_cast<const std::uint8_t*>(source.data()),
                                         2,
                                         1,
                                         sizeof(source),
                                         DXGI_FORMAT_R10G10B10A2_UNORM,
                                         false,
                                         300.0f);

    bool passed = Check(converted.size() == 8, "SDR conversion produces RGBA8 pixels");
    passed &= Check(converted[0] == 255 && converted[1] == 0 && converted[2] >= 127 && converted[2] <= 128 &&
                        converted[3] == 255,
                    "SDR conversion unpacks R10G10B10A2");
    passed &= Check(converted[4] == 0 && converted[5] == 255 && converted[6] == 0 && converted[7] == 0,
                    "SDR conversion preserves the second pixel and alpha");
    return passed;
}

float NitsToPq(float nits)
{
    const float m1 = 2610.0f / 16384.0f;
    const float m2 = 2523.0f / 32.0f;
    const float c1 = 3424.0f / 4096.0f;
    const float c2 = 2413.0f / 128.0f;
    const float c3 = 2392.0f / 128.0f;
    const float normalized = std::clamp(nits / 10000.0f, 0.0f, 1.0f);
    const float powered = std::pow(normalized, m1);
    return std::pow((c1 + c2 * powered) / (1.0f + c3 * powered), m2);
}

bool TestHdr10Conversion()
{
    const std::uint32_t paperWhiteCode = static_cast<std::uint32_t>(std::lround(NitsToPq(300.0f) * 1023.0f));
    const std::array<std::uint32_t, 1> source = {
        PackR10G10B10A2(paperWhiteCode, paperWhiteCode, paperWhiteCode, 3),
    };
    const std::vector<std::uint8_t> converted =
        Engine::ConvertScreenshotToRgba8(reinterpret_cast<const std::uint8_t*>(source.data()),
                                         1,
                                         1,
                                         sizeof(source),
                                         DXGI_FORMAT_R10G10B10A2_UNORM,
                                         true,
                                         300.0f);

    return Check(converted.size() == 4 && converted[0] >= 253 && converted[1] >= 253 && converted[2] >= 253 &&
                     converted[3] == 255,
                 "HDR10 paper white converts to displayable SDR white");
}

bool TestPngEncoding()
{
    const std::array<std::uint8_t, 16> rgba = {
        255,
        0,
        0,
        255,
        0,
        255,
        0,
        255,
        0,
        0,
        255,
        255,
        255,
        255,
        255,
        255,
    };
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "RtPbrSurvey.ScreenshotTests.png";
    std::string error;
    bool passed = Check(Engine::SaveRgba8Png(path, 2, 2, rgba.data(), error), "PNG encoding succeeds");
    if (!passed)
    {
        std::cerr << error << '\n';
        return false;
    }

    std::ifstream stream(path, std::ios::binary);
    const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    const std::array<std::uint8_t, 8> signature = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};

    passed &= Check(bytes.size() >= 24, "PNG contains a complete signature and IHDR");
    passed &= Check(std::equal(signature.begin(), signature.end(), bytes.begin()), "PNG signature is valid");
    passed &= Check(ReadBigEndian32(bytes.data() + 16) == 2 && ReadBigEndian32(bytes.data() + 20) == 2,
                    "PNG dimensions are encoded in IHDR");

    std::error_code removeError;
    std::filesystem::remove(path, removeError);
    return passed;
}
} // namespace

int main()
{
    if (TestSdrConversion() && TestHdr10Conversion() && TestPngEncoding())
    {
        std::cout << "Screenshot tests passed.\n";
        return 0;
    }
    return 1;
}
