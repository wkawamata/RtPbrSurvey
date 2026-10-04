#include "stdafx.h"

#include "Platform/CommandLineOptions.h"
#include "Runtime/CaptureSession.h"
#include "Runtime/CaptureSessionUi.h"

#include <iostream>
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
    bool passed = Check(!session.Start(config, error) && !error.empty(), "unimplemented GIF format fails at start");

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
    WCHAR formatOption[] = L"-CaptureSessionFormat";
    WCHAR format[] = L"exr";
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
        formatOption,
        format,
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
    const Platform::CommandLineOptions sessionOptions = Platform::ParseCommandLineOptions(sessionArgv, 16);
    passed &= Check(sessionOptions.captureSessionEnabled && sessionOptions.captureSessionOutputDirectory == "Captures" &&
                        sessionOptions.captureSessionBaseName == L"turntable" &&
                        sessionOptions.captureSessionFormat == L"exr" && sessionOptions.captureSessionFrameLimit == 12,
                    "Capture Session CLI parses common output settings");
    RtPbrSurvey::CaptureSessionConfig sessionConfig;
    std::string configError;
    passed &= Check(Platform::BuildCaptureSessionConfig(sessionOptions, sessionConfig, configError),
                    "Capture Session CLI builds shared config");
    passed &= Check(sessionConfig.durationSeconds == 2.5 && sessionConfig.region.has_value() &&
                        sessionConfig.region->x == 8 && sessionConfig.region->height == 180 &&
                        sessionConfig.source == RtPbrSurvey::ScreenshotCaptureSource::PreToneMapSceneColor,
                    "shared CLI config preserves ROI, duration, and EXR source");
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
} // namespace

int main()
{
    return TestStableOutputPath() && TestStableStopButton() && TestOutputOrderAndStopDrain() && TestOutputNumbering() && TestRealTimeDropAndFixedStepBackpressure() &&
                   TestValidationAndLegacyCli() && TestOutputFailureCompletesCleanup() && TestWarmupExcludedFromDuration() &&
                   TestMismatchedResultDoesNotCompleteSession() ?
        0 :
        1;
}
