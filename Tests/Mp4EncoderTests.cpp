#include "stdafx.h"

#include "Renderer/Mp4Encoder.h"
#include "Platform/CommandLineOptions.h"
#include "Runtime/CaptureSession.h"
#include "Runtime/CaptureSessionUi.h"

#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <vector>
#include <mfapi.h>
#include <mferror.h>

namespace
{
    bool Check(bool condition, const char* message)
    {
        if (!condition) std::cerr << "FAILED: " << message << '\n';
        return condition;
    }

    bool InspectVideo(const std::filesystem::path& path, unsigned int expectedWidth, unsigned int expectedHeight,
                      unsigned int expectedFrames, unsigned int fps, bool checkColors,
                      const std::vector<LONGLONG>& expectedTimes = {},
                      std::optional<LONGLONG> expectedEndTime = std::nullopt)
    {
        Microsoft::WRL::ComPtr<IMFSourceReader> reader;
        HRESULT result = MFCreateSourceReaderFromURL(path.c_str(), nullptr, &reader);
        if (!Check(SUCCEEDED(result), "finalized MP4 is readable")) return false;
        Microsoft::WRL::ComPtr<IMFMediaType> nativeType;
        result = reader->GetNativeMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, &nativeType);
        GUID subtype = {};
        UINT32 width = 0, height = 0, numerator = 0, denominator = 0;
        if (SUCCEEDED(result)) result = nativeType->GetGUID(MF_MT_SUBTYPE, &subtype);
        if (SUCCEEDED(result)) result = MFGetAttributeSize(nativeType.Get(), MF_MT_FRAME_SIZE, &width, &height);
        if (SUCCEEDED(result)) result = MFGetAttributeRatio(nativeType.Get(), MF_MT_FRAME_RATE, &numerator, &denominator);
        bool passed = Check(SUCCEEDED(result) && subtype == MFVideoFormat_H264 &&
                            width == expectedWidth && height == expectedHeight &&
                            (expectedFrames == 0 || !expectedTimes.empty() || numerator == fps * denominator),
                            "MP4 contains H.264 at the requested size and fixed-step FPS");
        Microsoft::WRL::ComPtr<IMFMediaType> decodedType;
        result = MFCreateMediaType(&decodedType);
        if (SUCCEEDED(result)) result = decodedType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        if (SUCCEEDED(result)) result = decodedType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
        if (SUCCEEDED(result)) result = reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, decodedType.Get());
        if (!Check(SUCCEEDED(result), "H.264 stream can be decoded to NV12")) return false;
        unsigned int count = 0;
        LONGLONG lastTime = -1;
        LONGLONG endTime = 0;
        for (unsigned int reads = 0; reads < (expectedFrames ? expectedFrames + 16 : 10000); ++reads)
        {
            DWORD flags = 0;
            LONGLONG time = 0;
            Microsoft::WRL::ComPtr<IMFSample> sample;
            result = reader->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, nullptr, &flags, &time, &sample);
            if (!Check(SUCCEEDED(result) && !(flags & MF_SOURCE_READERF_ERROR), "video sample decodes successfully")) return false;
            if (sample)
            {
                passed &= Check(time > lastTime, "video timestamps are ordered");
                if (expectedFrames != 0)
                {
                    const LONGLONG expectedTime = expectedTimes.empty() ?
                        static_cast<LONGLONG>(count * 10000000ull / fps) :
                        expectedTimes[(std::min)(static_cast<std::size_t>(count), expectedTimes.size() - 1)];
                    passed &= Check(std::abs(time - expectedTime) <= 1000, "video retains the submitted presentation time");
                }
                lastTime = time;
                LONGLONG duration = 0;
                passed &= Check(SUCCEEDED(sample->GetSampleDuration(&duration)) && duration > 0,
                                "decoded samples retain frame duration");
                endTime = time + duration;
                if (checkColors && count == 0)
                {
                    Microsoft::WRL::ComPtr<IMFMediaBuffer> buffer;
                    result = sample->ConvertToContiguousBuffer(&buffer);
                    BYTE* pixels = nullptr;
                    DWORD length = 0;
                    if (SUCCEEDED(result)) result = buffer->Lock(&pixels, nullptr, &length);
                    if (!Check(SUCCEEDED(result), "decoded pixels are accessible")) return false;
                    // Solid red top half and solid blue bottom half: verify orientation and UV order.
                    const std::size_t chroma = static_cast<std::size_t>(width) * height;
                    passed &= Check(length >= chroma * 3 / 2 && pixels[8 * width + 8] > 55 &&
                                    pixels[(height - 8) * width + 8] < 45 &&
                                    pixels[chroma + 4 * width + 8] < 120 && pixels[chroma + 4 * width + 9] > 210,
                                    "decoded color channels and top-down orientation are preserved");
                    buffer->Unlock();
                }
                ++count;
            }
            if (flags & MF_SOURCE_READERF_ENDOFSTREAM) break;
        }
        passed &= Check(expectedFrames ? count == expectedFrames : count > 0, "all encoded frames decode, with no extras");
        const LONGLONG expectedEnd = expectedEndTime.value_or(static_cast<LONGLONG>(expectedFrames * 10000000ull / fps));
        passed &= Check(std::abs(endTime - expectedEnd) <= (expectedFrames ? 1000 : 500000),
                        "video duration follows the capture timeline");
        std::cout << "H.264 " << width << 'x' << height << ", " << count << " frames, " << fps << " FPS" << std::endl;
        return passed;
    }

    bool TestEncoding()
    {
        const auto directory = std::filesystem::temp_directory_path() /
            ("RtPbrSurveyMp4Tests_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        const auto path = directory / L"\u52d5\u753b.mp4";
        std::vector<std::uint8_t> pixels(63 * 47 * 4, 255);
        for (unsigned int y = 0; y < 47; ++y)
        {
            for (unsigned int x = 0; x < 63; ++x)
            {
                const auto offset = (y * 63 + x) * 4;
                pixels[offset] = y < 24 ? 255 : 0;
                pixels[offset + 1] = 0;
                pixels[offset + 2] = y < 24 ? 0 : 255;
            }
        }
        Engine::Mp4Encoder encoder;
        std::string error;
        bool passed = true;
        for (unsigned int frame = 0; frame < 3; ++frame)
        {
            passed &= Check(encoder.AppendFrame(path, 63, 47, pixels.data(), 60, 12000000, error), error.c_str());
            if (!passed) break;
        }
        passed &= Check(encoder.Finalize(error), error.c_str());
        if (passed) passed &= InspectVideo(path, 64, 48, 3, 60, true);
        const auto originalSize = std::filesystem::exists(path) ? std::filesystem::file_size(path) : 0;
        passed &= Check(!encoder.AppendFrame(path, 63, 47, pixels.data(), 60, 12000000, error),
                        "encoder refuses to overwrite an existing MP4");
        passed &= Check(std::filesystem::exists(path) && std::filesystem::file_size(path) == originalSize,
                        "refusing overwrite preserves the completed MP4");
        const auto partial = directory / "partial.mp4";
        passed &= Check(encoder.AppendFrame(partial, 63, 47, pixels.data(), 30, 12000000, error), error.c_str());
        passed &= Check(!encoder.AppendFrame(partial, 62, 47, pixels.data(), 30, 12000000, error),
                        "changing frame dimensions fails instead of corrupting the stream");
        passed &= Check(!std::filesystem::exists(partial), "failed encoding removes the incomplete video");
        const auto realTimePath = directory / "realtime.mp4";
        const std::vector<LONGLONG> captureTimes = {0, 400000, 1000000};
        for (const LONGLONG time : captureTimes)
        {
            passed &= Check(encoder.AppendFrame(realTimePath, 63, 47, pixels.data(), 60, 12000000, error,
                                                static_cast<std::uint64_t>(time)), error.c_str());
        }
        passed &= Check(encoder.Finalize(error, 1500000), error.c_str());
        passed &= InspectVideo(realTimePath, 64, 48, 3, 60, false, captureTimes, 1500000);
        std::filesystem::remove_all(directory);
        return passed;
    }

    bool TestSessionConfiguration()
    {
        WCHAR executable[] = L"RtPbrSurvey.exe", output[] = L"-CaptureSessionOutputDir", directory[] = L"Captures";
        WCHAR base[] = L"-CaptureSessionBaseName", name[] = L"movie", format[] = L"-CaptureSessionFormat", mp4[] = L"mp4";
        WCHAR frames[] = L"-CaptureSessionFrames", count[] = L"3", bitrate[] = L"-CaptureSessionMp4BitrateMbps", rate[] = L"8";
        WCHAR* argv[] = {executable, output, directory, base, name, format, mp4, frames, count, bitrate, rate};
        const auto options = Platform::ParseCommandLineOptions(argv, static_cast<int>(std::size(argv)));
        RtPbrSurvey::CaptureSessionConfig config;
        std::string error;
        bool passed = Check(Platform::BuildCaptureSessionConfig(options, config, error) && config.mp4Bitrate == 8000000,
                            "CLI accepts MP4 with configurable bitrate");
        RtPbrSurvey::CaptureSessionUiState ui;
        ui.outputFormat = static_cast<int>(RtPbrSurvey::CaptureSessionOutputFormat::Mp4);
        ui.mp4BitrateMbps = 8;
        passed &= Check(RtPbrSurvey::CaptureSessionUi::BuildConfig(ui).mp4Bitrate == config.mp4Bitrate,
                        "GUI and CLI use the same MP4 bitrate");
        const auto testDirectory = std::filesystem::temp_directory_path() /
            ("RtPbrSurveyMp4Paths_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        config.outputDirectory = testDirectory;
        config.outputSubdirectory = "takes/center";
        std::filesystem::create_directories(testDirectory / config.outputSubdirectory);
        const auto existing = testDirectory / config.outputSubdirectory / "movie.mp4";
        std::ofstream(existing).put('x');
        RtPbrSurvey::CaptureSession session;
        passed &= Check(session.Start(config, error), error.c_str());
        session.Update({0, 0, 0});
        const auto request = session.AcquireReadyRequest();
        passed &= Check(request && request->path.filename() == "movie_000001.mp4" &&
                        request->outputFormat == RtPbrSurvey::ScreenshotOutputFormat::Mp4 && request->videoBitrate == 8000000 &&
                        request->videoFramesPerSecond == 60, "MP4 uses an unused filename and exact FPS in a subfolder");
        if (request)
        {
            session.MarkRequestAccepted();
            session.CompleteRequest({request->path, true, {}, 64, 48, request->requestId});
            session.Update({1, 0.1, 0.1});
            passed &= Check(session.AcquireReadyRequest() && session.AcquireReadyRequest()->path == request->path,
                            "all video frames use one output path");
        }
        session.Stop();
        passed &= Check(session.GetStatus().state == RtPbrSurvey::CaptureSessionState::Completed,
                        "Stop between video frames completes the session");
        config.mp4Bitrate = 0;
        passed &= Check(!session.Start(config, error), "invalid bitrate is rejected");
        config.mp4Bitrate = 8000000;
        config.source = RtPbrSurvey::ScreenshotCaptureSource::PreToneMapSceneColor;
        passed &= Check(!session.Start(config, error), "MP4 rejects an HDR scene-color source");
        config.source = RtPbrSurvey::ScreenshotCaptureSource::FinalOutput;
        config.durationSeconds = 0.15;
        config.frameLimit.reset();
        passed &= Check(session.Start(config, error), error.c_str());
        session.Update({0, 10.0, 0.0});
        const auto first = session.AcquireReadyRequest();
        passed &= Check(first && first->videoTimestamp100ns == 0, "real-time video starts at zero");
        if (first)
        {
            session.MarkRequestAccepted();
            session.Update({1, 10.03, 0.03});
            session.CompleteRequest({first->path, true, {}, 64, 48, first->requestId});
            session.Update({2, 10.04, 0.04});
            const auto next = session.AcquireReadyRequest();
            passed &= Check(next && next->videoTimestamp100ns == 400000 && session.GetStatus().droppedFrameCount > 0,
                            "dropped frames retain elapsed time instead of speeding up video");
            if (next)
            {
                session.MarkRequestAccepted();
                session.CompleteRequest({next->path, true, {}, 64, 48, next->requestId});
            }
            session.Update({3, 10.15, 0.15});
            session.Stop();
            passed &= Check(session.GetVideoEndTimestamp100ns() == 1500000, "video end time includes uncaptured trailing time");
        }
        std::filesystem::remove_all(testDirectory);
        return passed;
    }
}

int main(int argc, char** argv)
{
    const HRESULT comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(comResult)) return 1;
    const HRESULT mfResult = MFStartup(MF_VERSION);
    if (FAILED(mfResult))
    {
        CoUninitialize();
        return 1;
    }
    bool passed = false;
    if (argc == 7 && std::string(argv[1]) == "--duration")
    {
        passed = InspectVideo(argv[2], std::stoul(argv[3]), std::stoul(argv[4]), 0, std::stoul(argv[5]), false, {},
                              static_cast<LONGLONG>(std::llround(std::stod(argv[6]) * 10000000.0)));
    }
    else if (argc == 6)
    {
        passed = InspectVideo(argv[1], std::stoul(argv[2]), std::stoul(argv[3]), std::stoul(argv[4]), std::stoul(argv[5]), false);
    }
    else
    {
        passed = TestEncoding() && TestSessionConfiguration();
    }
    MFShutdown();
    CoUninitialize();
    return passed ? 0 : 1;
}
