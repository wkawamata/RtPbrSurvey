#include "stdafx.h"

#include "Platform/CommandLineOptions.h"
#include "Runtime/CaptureSession.h"

#include <iostream>

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
    session.CompleteRequest({first->path, true, {}, 1, 1});
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
    session.CompleteRequest({first->path, true, {}, 1, 1});
    session.Update({1, 0.1, 0.1});
    const auto second = session.AcquireReadyRequest();
    passed &= Check(second.has_value() && second->path.filename() == "frame_000001.png", "second output preserves ordering");
    session.MarkRequestAccepted();
    session.CompleteRequest({second->path, true, {}, 1, 1});
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
    realTime.CompleteRequest({request->path, true, {}, 1, 1});

    RtPbrSurvey::CaptureSession fixedStep;
    passed &= Check(fixedStep.Start(MakeConfig(RtPbrSurvey::CaptureSessionClock::FixedStep), error), "fixed-step session starts");
    fixedStep.Update({0, 0.0, 0.0});
    const auto fixedRequest = fixedStep.AcquireReadyRequest();
    passed &= Check(!fixedStep.CanAdvanceFixedStep(), "fixed-step host waits while a capture request is ready");
    fixedStep.MarkRequestAccepted();
    passed &= Check(fixedStep.CanAdvanceFixedStep(), "fixed-step host may advance after accepting the request");
    fixedStep.Update({1, 1.0, 1.0});
    passed &= Check(fixedStep.GetStatus().droppedFrameCount == 0, "fixed-step capture never drops busy frames");
    fixedStep.CompleteRequest({fixedRequest->path, true, {}, 1, 1});
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
    };
    const Platform::CommandLineOptions sessionOptions = Platform::ParseCommandLineOptions(sessionArgv, 9);
    passed &= Check(sessionOptions.captureSessionEnabled && sessionOptions.captureSessionOutputDirectory == "Captures" &&
                        sessionOptions.captureSessionBaseName == L"turntable" &&
                        sessionOptions.captureSessionFormat == L"exr" && sessionOptions.captureSessionFrameLimit == 12,
                    "Capture Session CLI parses common output settings");
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
    session.CompleteRequest({request->path, false, "write failed"});
    passed &= Check(session.GetStatus().state == RtPbrSurvey::CaptureSessionState::Failed,
                    "output failure reaches failed after cleanup");
    passed &= Check(session.GetStatus().error == "write failed", "output failure is retained in status");
    return passed;
}
} // namespace

int main()
{
    return TestOutputOrderAndStopDrain() && TestOutputNumbering() && TestRealTimeDropAndFixedStepBackpressure() &&
                   TestValidationAndLegacyCli() && TestOutputFailureCompletesCleanup() ?
        0 :
        1;
}
