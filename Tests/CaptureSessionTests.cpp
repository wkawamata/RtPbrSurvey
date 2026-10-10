#include "stdafx.h"

#include "Platform/CommandLineOptions.h"
#include "Renderer/AnimatedGifEncoder.h"
#include "Runtime/CaptureSession.h"
#include "Runtime/CaptureSessionUi.h"
#include "Runtime/CaptureRequestGate.h"

#include <array>
#include <chrono>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>
#include <imgui.h>

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

RtPbrSurvey::CaptureSessionConfig MakeConfig(RtPbrSurvey::CaptureSessionClock clock)
{
    RtPbrSurvey::CaptureSessionConfig config;
    config.outputDirectory = "Captures";
    config.baseName = "frame";
    config.clock = clock;
    config.framesPerSecond = 60;
    config.frameLimit = 2;
    return config;
}

bool TestStableOutputPath()
{
    bool passed = true;
    for (bool singleOutput : {false, true})
    {
        RtPbrSurvey::CaptureSession session;
        auto config = MakeConfig(RtPbrSurvey::CaptureSessionClock::FixedStep);
        config.outputDirectory = "Captures/../Captures";
        if (singleOutput)
        {
            config.singleOutputPath = "Captures/../single.png";
            config.frameLimit = 1;
        }
        std::string error;
        passed &= Check(session.Start(config, error), "path session starts");
        session.Update({0, 0.0, 0.0});
        const auto request = session.AcquireReadyRequest();
        if (!request)
        {
            return Check(false, "path request exists");
        }
        passed &= Check(request->path.is_absolute() && request->path == request->path.lexically_normal(),
                        "request path is normalized and absolute");
        session.MarkRequestAccepted();
        const auto acceptedPath = session.GetStatus().lastOutputPath;
        session.CompleteRequest({request->path.filename(), true, {}, 1, 1, request->requestId});
        passed &= Check(session.GetStatus().lastOutputPath == acceptedPath,
                        "completion does not replace accepted path with renderer spelling");
    }
    return passed;
}

bool TestMouseRegionCoordinates()
{
    using Ui = RtPbrSurvey::CaptureSessionUi;
    const auto forward = Ui::RegionFromDrag(565, 285, 715, 435, 1280, 720, 2560, 1440);
    bool passed = Check(forward && forward->x == 1130 && forward->y == 570 &&
                        forward->width == 300 && forward->height == 300,
                        "mouse ROI maps logical coordinates to output pixels");
    const auto reverse = Ui::RegionFromDrag(715, 435, 565, 285, 1280, 720, 2560, 1440);
    passed &= Check(reverse && reverse->x == 1130 && reverse->y == 570 &&
                    reverse->width == 300 && reverse->height == 300, "reverse drag selects the same rectangle");
    const auto clamped = Ui::RegionFromDrag(-20, -10, 1400, 800, 1280, 720, 2560, 1440);
    passed &= Check(clamped && clamped->x == 0 && clamped->y == 0 &&
                    clamped->width == 2560 && clamped->height == 1440, "drag is clamped to output bounds");
    passed &= Check(!Ui::RegionFromDrag(10.5f, 10.5f, 10.5f, 10.5f, 1280, 720, 2560, 1440),
                    "a click cannot overwrite ROI with an empty selection");
    return passed;
}

bool TestMouseRegionInteraction()
{
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(800, 600);
    io.DeltaTime = 1.0f / 60.0f;
    unsigned char* pixels = nullptr;
    int width = 0;
    int height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    RtPbrSurvey::CaptureSessionStatus status;
    RtPbrSurvey::CaptureSessionUiState state;
    state.selectingRegion = true;
    const auto draw = [&]()
    {
        ImGui::NewFrame();
        RtPbrSurvey::CaptureSessionUi::DrawRegionOverlay(status, state, 1600, 1200);
        ImGui::Render();
    };
    draw();
    io.AddMousePosEvent(100, 80);
    draw();
    io.AddMouseButtonEvent(0, true);
    draw();
    io.AddMousePosEvent(400, 300);
    draw();
    io.AddMouseButtonEvent(0, false);
    draw();
    bool passed = Check(!state.selectingRegion && state.useRegion && state.showRegionOverlay &&
                        state.regionX == 200 && state.regionY == 160 &&
                        state.regionWidth == 600 && state.regionHeight == 440,
                        "mouse release commits the scaled selection to numeric ROI fields");
    state.selectingRegion = true;
    draw();
    io.AddMouseButtonEvent(1, true);
    draw();
    passed &= Check(!state.selectingRegion && state.regionWidth == 600,
                    "right-click cancels while retaining the previous ROI");
    io.AddMouseButtonEvent(1, false);
    draw();
    state.selectingRegion = true;
    io.AddKeyEvent(ImGuiKey_Escape, true);
    draw();
    passed &= Check(!state.selectingRegion && state.regionHeight == 440,
                    "Escape cancels without changing the selected region");
    io.AddKeyEvent(ImGuiKey_Escape, false);
    draw();
    state.selectingRegion = true;
    io.AddMousePosEvent(700, 500);
    draw();
    io.AddMouseButtonEvent(0, true);
    draw();
    io.AddMousePosEvent(900, 700);
    draw();
    io.AddMouseButtonEvent(0, false);
    draw();
    passed &= Check(!state.selectingRegion && state.regionX == 1400 && state.regionY == 1000 &&
                        state.regionWidth == 200 && state.regionHeight == 200,
                    "release beyond display bounds commits a clamped ROI");
    state.showRegionOverlay = false;
    draw();
    passed &= Check(ImGui::GetDrawData()->CmdListsCount == 0, "overlay toggle hides the selected region");
    state.showRegionOverlay = true;
    draw();
    passed &= Check(ImGui::GetDrawData()->CmdListsCount > 0, "ROI stays visible without a settings panel");
    state.selectingRegion = true;
    status.state = RtPbrSurvey::CaptureSessionState::Recording;
    draw();
    passed &= Check(!state.selectingRegion && ImGui::GetDrawData()->CmdListsCount == 0,
                    "capture suppresses ROI selection and overlay drawing");
    ImGui::DestroyContext();
    return passed;
}

bool TestStableStopButton()
{
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(800, 800);
    io.DeltaTime = 1.0f / 60.0f;
    unsigned char* pixels = nullptr;
    int width = 0;
    int height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    RtPbrSurvey::CaptureSessionStatus status;
    status.state = RtPbrSurvey::CaptureSessionState::Recording;
    RtPbrSurvey::CaptureSessionUiState state;
    ImVec2 stopPosition;
    const auto draw = [&]()
    {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(440, 780));
        ImGui::Begin("Capture test", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize);
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const ImGuiStyle& style = ImGui::GetStyle();
        stopPosition = ImVec2(origin.x + ImGui::CalcTextSize("Start Capture Session").x +
                                 style.FramePadding.x * 2 + style.ItemSpacing.x + 5,
                             origin.y + ImGui::GetFrameHeight() * 0.5f);
        const auto action = RtPbrSurvey::CaptureSessionUi::Draw(status, state);
        ImGui::End();
        ImGui::Render();
        return action;
    };
    draw();
    io.AddMousePosEvent(stopPosition.x, stopPosition.y);
    draw();
    io.AddMouseButtonEvent(0, true);
    draw();
    status.lastOutputPath = std::string(240, 'x') + "/frame_000001.png";
    status.error = "A variable-height diagnostic appears while the mouse button is held.";
    state.message = std::string(160, 'm');
    io.AddMouseButtonEvent(0, false);
    const bool passed = Check(draw() == RtPbrSurvey::CaptureSessionUiAction::Stop,
                              "Stop retains its click target and ID when wrapped status changes");
    ImGui::DestroyContext();
    return passed;
}

bool TestStartButtonAvailability()
{
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(800, 800);
    io.DeltaTime = 1.0f / 60.0f;
    unsigned char* pixels = nullptr;
    int width = 0;
    int height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

    RtPbrSurvey::CaptureSessionStatus status;
    RtPbrSurvey::CaptureSessionUiState state;
    ImVec2 startPosition;
    const auto draw = [&status, &state, &startPosition](const char* startBlockedReason)
    {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(440, 780));
        ImGui::Begin("Capture start test", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize);
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        startPosition = ImVec2(origin.x + 5.0f, origin.y + ImGui::GetFrameHeight() * 0.5f);
        const RtPbrSurvey::CaptureSessionUiAction action =
            RtPbrSurvey::CaptureSessionUi::Draw(status, state, startBlockedReason);
        ImGui::End();
        ImGui::Render();
        return action;
    };
    const auto clickStart = [&draw, &startPosition](const char* startBlockedReason)
    {
        draw(startBlockedReason);
        ImGui::GetIO().AddMousePosEvent(startPosition.x, startPosition.y);
        draw(startBlockedReason);
        ImGui::GetIO().AddMouseButtonEvent(0, true);
        draw(startBlockedReason);
        ImGui::GetIO().AddMouseButtonEvent(0, false);
        return draw(startBlockedReason);
    };

    bool passed = Check(clickStart(nullptr) == RtPbrSurvey::CaptureSessionUiAction::Start,
                        "Start is available after a terminal capture status");
    status.state = RtPbrSurvey::CaptureSessionState::Recording;
    passed &= Check(clickStart(nullptr) == RtPbrSurvey::CaptureSessionUiAction::None,
                    "Start is disabled while recording");
    status.state = RtPbrSurvey::CaptureSessionState::Failed;
    passed &= Check(clickStart("A screenshot request is still saving.") == RtPbrSurvey::CaptureSessionUiAction::None,
                    "Start is disabled while the host capture gate is blocked");
    ImGui::DestroyContext();
    return passed;
}

bool TestOutputOrderAndStopDrain()
{
    RtPbrSurvey::CaptureSession session;
    std::string error;
    bool passed = Check(session.Start(MakeConfig(RtPbrSurvey::CaptureSessionClock::RealTime), error), "session starts");

    session.Update({0, 0.0, 0.0});
    const auto first = session.AcquireReadyRequest();
    passed &= Check(first.has_value() && first->path.filename() == "frame_000000.png", "first output uses numbered path");
    session.MarkRequestAccepted();
    session.Stop();
    passed &= Check(session.GetStatus().state == RtPbrSurvey::CaptureSessionState::Draining, "stop drains an accepted request");
    session.CompleteRequest({first->path, true, {}, 1, 1, first->requestId});
    passed &= Check(session.GetStatus().state == RtPbrSurvey::CaptureSessionState::Completed, "drain completes after output");
    passed &= Check(session.GetStatus().savedFrameCount == 1, "drain preserves completed output");
    return passed;
}

bool TestOutputNumbering()
{
    RtPbrSurvey::CaptureSession session;
    std::string error;
    bool passed = Check(session.Start(MakeConfig(RtPbrSurvey::CaptureSessionClock::FixedStep), error), "fixed session starts");
    session.Update({0, 0.0, 0.0});
    const auto first = session.AcquireReadyRequest();
    session.MarkRequestAccepted();
    session.CompleteRequest({first->path, true, {}, 1, 1, first->requestId});
    session.Update({1, 0.1, 0.1});
    const auto second = session.AcquireReadyRequest();
    passed &= Check(second.has_value() && second->path.filename() == "frame_000001.png", "second output preserves ordering");
    session.MarkRequestAccepted();
    session.CompleteRequest({second->path, true, {}, 1, 1, second->requestId});
    passed &= Check(session.GetStatus().state == RtPbrSurvey::CaptureSessionState::Completed, "frame limit completes after final result");
    return passed;
}

bool TestRealTimeDropAndFixedStepBackpressure()
{
    std::string error;
    RtPbrSurvey::CaptureSession realTime;
    bool passed = Check(realTime.Start(MakeConfig(RtPbrSurvey::CaptureSessionClock::RealTime), error), "real-time session starts");
    realTime.Update({0, 0.0, 0.0});
    const auto request = realTime.AcquireReadyRequest();
    realTime.MarkRequestAccepted();
    realTime.Update({1, 0.050, 0.050});
    passed &= Check(realTime.GetStatus().droppedFrameCount == 3, "real-time busy frames are explicitly dropped");
    realTime.CompleteRequest({request->path, true, {}, 1, 1, request->requestId});

    RtPbrSurvey::CaptureSession fixedStep;
    passed &= Check(fixedStep.Start(MakeConfig(RtPbrSurvey::CaptureSessionClock::FixedStep), error), "fixed-step session starts");
    fixedStep.Update({0, 0.0, 0.0});
    const auto fixedRequest = fixedStep.AcquireReadyRequest();
    passed &= Check(!fixedStep.CanAdvanceFixedStep(), "fixed-step host waits while a capture request is ready");
    fixedStep.MarkRequestAccepted();
    passed &= Check(!fixedStep.CanAdvanceFixedStep(), "fixed-step host waits while renderer readback is in flight");
    fixedStep.Update({1, 1.0, 1.0});
    passed &= Check(fixedStep.GetStatus().droppedFrameCount == 0, "fixed-step capture never drops busy frames");
    fixedStep.CompleteRequest({fixedRequest->path, true, {}, 1, 1, fixedRequest->requestId});
    passed &= Check(fixedStep.CanAdvanceFixedStep(), "fixed-step host advances after its renderer result is polled");
    fixedStep.Update({2, 1.1, 1.1});
    passed &= Check(fixedStep.AcquireReadyRequest().has_value(), "fixed-step capture waits for and schedules the required frame");
    return passed;
}

bool TestValidationAndLegacyCli()
{
    RtPbrSurvey::CaptureSession session;
    RtPbrSurvey::CaptureSessionConfig config = MakeConfig(RtPbrSurvey::CaptureSessionClock::RealTime);
    config.outputFormat = RtPbrSurvey::CaptureSessionOutputFormat::Gif;
    std::string error;
    bool passed = Check(session.Start(config, error), "GIF format starts");
    session.Update({0, 0.0, 0.0});
    const auto gifFirst = session.AcquireReadyRequest();
    passed &= Check(gifFirst.has_value() && gifFirst->path.filename() == "frame.gif" &&
                        gifFirst->outputFormat == RtPbrSurvey::ScreenshotOutputFormat::Gif &&
                        gifFirst->frameDelayCentiseconds == 2 && gifFirst->gifRepeatCount == 0 &&
                        gifFirst->gifDisposal == static_cast<std::uint8_t>(RtPbrSurvey::CaptureSessionGifDisposal::Keep),
                    "GIF uses one output path, centisecond frame delay, infinite repeat, and frame disposal");
    session.MarkRequestAccepted();
    session.CompleteRequest({gifFirst->path, true, {}, 1, 1, gifFirst->requestId});
    session.Update({1, 0.1, 0.1});
    const auto gifSecond = session.AcquireReadyRequest();
    passed &= Check(gifSecond.has_value() && gifSecond->path == gifFirst->path,
                    "GIF frames append to the same output path");

    WCHAR executable[] = L"RtPbrSurvey.exe";
    WCHAR option[] = L"-CapturePath";
    WCHAR path[] = L"legacy.png";
    WCHAR* argv[] = {executable, option, path};
    const Platform::CommandLineOptions options = Platform::ParseCommandLineOptions(argv, 3);
    passed &= Check(options.capturePath == "legacy.png", "legacy CapturePath CLI remains supported");

    WCHAR outputDirectoryOption[] = L"-CaptureSessionOutputDir";
    WCHAR outputDirectory[] = L"Captures";
    WCHAR baseNameOption[] = L"-CaptureSessionBaseName";
    WCHAR baseName[] = L"turntable";
    WCHAR subfolderOption[] = L"-CaptureSessionSubfolder";
    WCHAR subfolder[] = L"run01";
    WCHAR formatOption[] = L"-CaptureSessionFormat";
    WCHAR format[] = L"exr";
    WCHAR gifRepeatOption[] = L"-CaptureSessionGifRepeat";
    WCHAR gifRepeat[] = L"3";
    WCHAR gifDisposalOption[] = L"-CaptureSessionGifDisposal";
    WCHAR gifDisposal[] = L"background";
    WCHAR frameOption[] = L"-CaptureSessionFrames";
    WCHAR frames[] = L"12";
    WCHAR roiOption[] = L"-CaptureSessionRoi";
    WCHAR roiX[] = L"8";
    WCHAR roiY[] = L"12";
    WCHAR roiWidth[] = L"320";
    WCHAR roiHeight[] = L"180";
    WCHAR durationOption[] = L"-CaptureSessionDurationSeconds";
    WCHAR duration[] = L"2.5";
    WCHAR* sessionArgv[] = {
        executable,
        outputDirectoryOption,
        outputDirectory,
        baseNameOption,
        baseName,
        subfolderOption,
        subfolder,
        formatOption,
        format,
        gifRepeatOption,
        gifRepeat,
        gifDisposalOption,
        gifDisposal,
        frameOption,
        frames,
        roiOption,
        roiX,
        roiY,
        roiWidth,
        roiHeight,
        durationOption,
        duration,
    };
    const Platform::CommandLineOptions sessionOptions = Platform::ParseCommandLineOptions(sessionArgv, 22);
    passed &= Check(sessionOptions.captureSessionEnabled && sessionOptions.captureSessionOutputDirectory == "Captures" &&
                        sessionOptions.captureSessionBaseName == L"turntable" &&
                        sessionOptions.captureSessionOutputSubdirectory == "run01" &&
                        sessionOptions.captureSessionGifRepeat == L"3" &&
                        sessionOptions.captureSessionGifDisposal == L"background" &&
                        sessionOptions.captureSessionFormat == L"exr" && sessionOptions.captureSessionFrameLimit == 12,
                    "Capture Session CLI parses common output settings");
    RtPbrSurvey::CaptureSessionConfig sessionConfig;
    std::string configError;
    passed &= Check(Platform::BuildCaptureSessionConfig(sessionOptions, sessionConfig, configError),
                    "Capture Session CLI builds shared config");
    passed &= Check(sessionConfig.outputSubdirectory == "run01" &&
                        sessionConfig.gifRepeatMode == RtPbrSurvey::CaptureSessionGifRepeatMode::Count &&
                        sessionConfig.gifRepeatCount == 3 &&
                        sessionConfig.gifDisposal == RtPbrSurvey::CaptureSessionGifDisposal::Background &&
                        sessionConfig.durationSeconds == 2.5 && sessionConfig.region.has_value() &&
                        sessionConfig.region->x == 8 && sessionConfig.region->height == 180 &&
                        sessionConfig.source == RtPbrSurvey::ScreenshotCaptureSource::PreToneMapSceneColor,
                    "shared CLI config preserves ROI, duration, and EXR source");
    return passed;
}

bool TestGifOutputPathDoesNotOverwrite()
{
    const std::filesystem::path root = std::filesystem::temp_directory_path() /
        ("RtPbrSurveyCaptureSessionTests_" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const std::filesystem::path subfolder = root / "take";
    std::error_code fileError;
    std::filesystem::create_directories(subfolder, fileError);
    if (fileError)
    {
        return Check(false, "GIF collision test directory is created");
    }

    const std::filesystem::path existingGif = subfolder / "frame.gif";
    std::ofstream(existingGif, std::ios::binary).put('\0');

    RtPbrSurvey::CaptureSession session;
    auto config = MakeConfig(RtPbrSurvey::CaptureSessionClock::FixedStep);
    config.outputDirectory = root;
    config.outputSubdirectory = "take";
    config.outputFormat = RtPbrSurvey::CaptureSessionOutputFormat::Gif;
    std::string error;
    bool passed = Check(session.Start(config, error), "GIF collision session starts");
    session.Update({0, 0.0, 0.0});
    const auto request = session.AcquireReadyRequest();
    passed &= Check(request.has_value() && request->path == (subfolder / "frame_000001.gif"),
                    "GIF collision appends a numbered suffix below the selected subfolder");

    std::filesystem::remove_all(root, fileError);
    return passed;
}

bool TestGifMetadataEncoding()
{
    const std::filesystem::path path = std::filesystem::temp_directory_path() /
        ("RtPbrSurveyAnimatedGifTests_" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".gif");
    const std::vector<std::uint8_t> rgba = {255, 0, 0, 255};
    Engine::AnimatedGifEncoder encoder;
    std::string error;
    bool passed = Check(encoder.AppendFrame(path, 1, 1, rgba.data(), 5, 3, 2, error),
                        "GIF encoder writes a frame with repeat and disposal metadata");
    passed &= Check(encoder.Finalize(error), "GIF encoder finalizes metadata test output");

    std::ifstream input(path, std::ios::binary);
    const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    const std::string text(bytes.begin(), bytes.end());
    const size_t applicationOffset = text.find("NETSCAPE2.0");
    passed &= Check(applicationOffset != std::string::npos && applicationOffset + 15 < bytes.size() &&
                        bytes[applicationOffset + 11] == 3 && bytes[applicationOffset + 12] == 1 &&
                        bytes[applicationOffset + 13] == 3 && bytes[applicationOffset + 14] == 0 &&
                        bytes[applicationOffset + 15] == 0,
                    "GIF repeat count is written to the NETSCAPE extension");
    constexpr std::array<std::uint8_t, 3> graphicControlSignature = {0x21, 0xf9, 0x04};
    const auto graphicControl = std::search(bytes.begin(), bytes.end(),
                                            graphicControlSignature.begin(), graphicControlSignature.end());
    passed &= Check(graphicControl != bytes.end() && ((*(graphicControl + 3) >> 2) & 0x07) == 2,
                    "GIF graphic control writes the requested disposal mode");

    std::error_code fileError;
    std::filesystem::remove(path, fileError);
    return passed;
}

bool TestOutputFailureCompletesCleanup()
{
    RtPbrSurvey::CaptureSession session;
    std::string error;
    bool passed = Check(session.Start(MakeConfig(RtPbrSurvey::CaptureSessionClock::RealTime), error), "failure session starts");
    session.Update({0, 0.0, 0.0});
    const auto request = session.AcquireReadyRequest();
    session.MarkRequestAccepted();
    session.CompleteRequest({request->path, false, "write failed", 0, 0, request->requestId});
    passed &= Check(session.GetStatus().state == RtPbrSurvey::CaptureSessionState::Failed,
                    "output failure reaches failed after cleanup");
    passed &= Check(session.GetStatus().error == "write failed", "output failure is retained in status");
    passed &= Check(session.Start(MakeConfig(RtPbrSurvey::CaptureSessionClock::RealTime), error),
                    "a failed capture session can be started again");
    passed &= Check(session.GetStatus().state == RtPbrSurvey::CaptureSessionState::Recording,
                    "restart resets the terminal failure state");
    return passed;
}

bool TestWarmupExcludedFromDuration()
{
    RtPbrSurvey::CaptureSessionConfig config = MakeConfig(RtPbrSurvey::CaptureSessionClock::RealTime);
    config.frameLimit.reset();
    config.warmupFrames = 3;
    config.durationSeconds = 1.0;
    RtPbrSurvey::CaptureSession session;
    std::string error;
    bool passed = Check(session.Start(config, error), "duration session starts");
    session.Update({0, 10.0, 10.0});
    session.Update({3, 20.0, 20.0});
    passed &= Check(session.GetStatus().state == RtPbrSurvey::CaptureSessionState::Recording,
                    "warmup completion starts recording");
    const auto request = session.AcquireReadyRequest();
    session.MarkRequestAccepted();
    session.Update({4, 20.9, 20.9});
    passed &= Check(session.GetStatus().state == RtPbrSurvey::CaptureSessionState::Recording,
                    "duration excludes warmup time");
    session.Update({5, 21.0, 21.0});
    passed &= Check(session.GetStatus().state == RtPbrSurvey::CaptureSessionState::Draining,
                    "duration drains an in-flight request from recording start");
    session.CompleteRequest({request->path, true, {}, 1, 1, request->requestId});
    passed &= Check(session.GetStatus().state == RtPbrSurvey::CaptureSessionState::Completed,
                    "duration completes after its in-flight result");
    return passed;
}

bool TestMismatchedResultDoesNotCompleteSession()
{
    RtPbrSurvey::CaptureSession session;
    std::string error;
    bool passed = Check(session.Start(MakeConfig(RtPbrSurvey::CaptureSessionClock::FixedStep), error),
                        "token session starts");
    session.Update({0, 0.0, 0.0});
    const auto request = session.AcquireReadyRequest();
    session.MarkRequestAccepted();
    session.CompleteRequest({request->path, true, {}, 1, 1, 0});
    passed &= Check(session.GetStatus().savedFrameCount == 0 && !session.CanAdvanceFixedStep(),
                    "legacy result cannot complete an in-flight session request");
    session.CompleteRequest({request->path, true, {}, 1, 1, request->requestId});
    passed &= Check(session.GetStatus().savedFrameCount == 1 && session.CanAdvanceFixedStep(),
                    "matching session token completes the request");
    return passed;
}

bool TestCaptureRequestGate()
{
    using RtPbrSurvey::CaptureRequestGate;
    using RtPbrSurvey::PendingHostAction;

    CaptureRequestGate gate;
    bool previewPending = true;
    CaptureRequestGate::Inputs previewInputs;
    bool previewPassed = true;
    for (int source = 0; source < 3; ++source)
    {
        previewInputs = {};
        previewInputs.captureSessionActive = source == 0;
        previewInputs.singleScreenshotInFlight = source == 1;
        previewInputs.diagnosticCaptureInFlight = source == 2;
        previewPending = true;
        gate.Update(previewInputs);
        previewPassed &= Check(!gate.TakePendingPreviewRebuild(previewPending, true) && previewPending,
                               "preview remains pending while any capture output is in flight");
        gate.Update({});
        previewPassed &= Check(gate.TakePendingPreviewRebuild(previewPending, true) && !previewPending,
                               "preview is consumed after capture output completes");
        previewPassed &= Check(!gate.TakePendingPreviewRebuild(previewPending, true),
                               "consumed preview is not retried, including after a rebuild failure");
    }
    previewPending = true;
    gate.Update(previewInputs);
    previewPassed &= Check(!gate.TakePendingPreviewRebuild(previewPending, false) && !previewPending,
                           "leaving edit mode cancels the old document preview request");
    gate.Update({});
    if (!previewPassed)
    {
        return false;
    }
    std::string reason;
    bool passed = Check(gate.CanStart(reason), "start is allowed when no capture work exists");

    gate.Update({true, false, false, false, false});
    passed &= Check(!gate.CanStart(reason) && reason == "A capture session is already active.",
                    "start is refused while a capture session is active");
    gate.Update({false, true, false, false, false});
    passed &= Check(!gate.CanStart(reason) && reason == "A single screenshot request is still saving.",
                    "start is refused while a single screenshot request is saving");
    gate.Update({false, false, true, false, false});
    passed &= Check(!gate.CanStart(reason) &&
                        reason == "Capture session is unavailable while automated capture is pending.",
                    "start is refused while an automated capture is pending");
    gate.Update({false, false, false, true, false});
    passed &= Check(!gate.CanStart(reason) && reason == "A diagnostic capture is still saving.",
                    "start is refused while a diagnostic capture is saving");

    // A completed or failed automation is not pending work, so a recorded host action is not
    // blocked forever and a later request can start.
    RtPbrSurvey::CaptureSessionStatus failedStatus;
    failedStatus.state = RtPbrSurvey::CaptureSessionState::Failed;
    passed &= Check(!RtPbrSurvey::CaptureSessionUi::IsActive(failedStatus),
                    "failed state is not pending capture work");

    gate.Update({true, false, false, false, false});
    passed &= Check(gate.RequestPendingAction(PendingHostAction::SceneEditorReturnToTopMenu) ==
                        PendingHostAction::SceneEditorReturnToTopMenu,
                    "scene switch is recorded while capture work is pending");
    passed &= Check(!gate.CanStart(reason) &&
                        reason == "A pending exit or scene switch is waiting for capture output to complete.",
                    "start is refused while a host action is pending");
    passed &= Check(!gate.CanExecutePendingAction(),
                    "pending action does not run while capture work is pending");
    passed &= Check(gate.RequestPendingAction(PendingHostAction::CloseApplication) ==
                        PendingHostAction::CloseApplication,
                    "application close outranks the recorded scene switch");
    passed &= Check(gate.RequestPendingAction(PendingHostAction::CloseRunningScene) ==
                        PendingHostAction::CloseApplication,
                    "a duplicate request keeps the higher priority action");
    passed &= Check(gate.TakeResolvedAction() == PendingHostAction::None,
                    "pending action still waits while capture work is pending");

    // A confirmation decision must be resolved before the recorded action runs.
    gate.Update({false, false, false, false, true});
    passed &= Check(!gate.CanExecutePendingAction(), "pending action waits for the confirmation decision");
    gate.Update({false, false, false, false, false});
    passed &= Check(gate.TakeResolvedAction() == PendingHostAction::CloseApplication,
                    "pending action runs once after capture output and the decision are resolved");
    passed &= Check(gate.TakeResolvedAction() == PendingHostAction::None,
                    "pending action is consumed once");
    passed &= Check(gate.CanStart(reason), "start is allowed again after the pending action ran");

    // Cancel releases the recorded action so editing continues.
    gate.RequestPendingAction(PendingHostAction::SceneEditorLoadDocument);
    gate.ClearPendingAction();
    passed &= Check(gate.GetPendingAction() == PendingHostAction::None, "cancel releases the pending action");
    return passed;
}
} // namespace

int main()
{
    return TestMouseRegionCoordinates() && TestMouseRegionInteraction() && TestStableOutputPath() && TestStableStopButton() && TestStartButtonAvailability() && TestOutputOrderAndStopDrain() && TestOutputNumbering() && TestRealTimeDropAndFixedStepBackpressure() &&
                   TestValidationAndLegacyCli() && TestGifOutputPathDoesNotOverwrite() && TestGifMetadataEncoding() && TestOutputFailureCompletesCleanup() && TestWarmupExcludedFromDuration() &&
                   TestMismatchedResultDoesNotCompleteSession() && TestCaptureRequestGate() ?
        0 :
        1;
}
