#include "stdafx.h"

#include "Runtime/CaptureSessionUi.h"

#include "Runtime/SceneRenderer.h"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>

namespace RtPbrSurvey
{
    namespace
    {
        const char* GetStateName(CaptureSessionState state)
        {
            switch (state)
            {
                case CaptureSessionState::Idle:
                    return "Idle";
                case CaptureSessionState::Warmup:
                    return "Warmup";
                case CaptureSessionState::Recording:
                    return "Recording";
                case CaptureSessionState::Draining:
                    return "Draining";
                case CaptureSessionState::Completed:
                    return "Completed";
                case CaptureSessionState::Failed:
                    return "Failed";
                default:
                    return "Unknown";
            }
        }

    } // namespace

    CaptureSessionConfig CaptureSessionUi::BuildConfig(const CaptureSessionUiState& state)
    {
        CaptureSessionConfig config;
        config.outputDirectory = state.outputDirectory;
        config.baseName = state.baseName;
        config.outputFormat = static_cast<CaptureSessionOutputFormat>(state.outputFormat);
        config.source = config.outputFormat == CaptureSessionOutputFormat::Exr ?
            ScreenshotCaptureSource::PreToneMapSceneColor : ScreenshotCaptureSource::FinalOutput;
        config.clock = state.fixedStep ? CaptureSessionClock::FixedStep : CaptureSessionClock::RealTime;
        config.framesPerSecond = static_cast<std::uint32_t>((std::max)(0, state.framesPerSecond));
        config.warmupFrames = static_cast<std::uint32_t>((std::max)(0, state.warmupFrames));
        if (state.useFrameLimit)
        {
            config.frameLimit = static_cast<std::uint64_t>((std::max)(0, state.frameLimit));
        }
        if (state.useDurationLimit)
        {
            config.durationSeconds = static_cast<double>(state.durationSeconds);
        }
        if (state.useRegion)
        {
            config.region = ScreenshotRegion{
                static_cast<std::uint32_t>((std::max)(0, state.regionX)),
                static_cast<std::uint32_t>((std::max)(0, state.regionY)),
                static_cast<std::uint32_t>((std::max)(0, state.regionWidth)),
                static_cast<std::uint32_t>((std::max)(0, state.regionHeight)),
            };
        }
        return config;
    }

    bool CaptureSessionUi::IsActive(const CaptureSessionStatus& status)
    {
        return status.state == CaptureSessionState::Warmup || status.state == CaptureSessionState::Recording ||
               status.state == CaptureSessionState::Draining;
    }

    void CaptureSessionUi::Update(SceneRenderer& renderer, const CaptureSessionTiming& timing)
    {
        if (IsActive(renderer.GetCaptureSessionStatus()))
        {
            renderer.UpdateCaptureSession(timing);
        }
    }

    void CaptureSessionUi::Draw(SceneRenderer& renderer, CaptureSessionUiState& state)
    {
        const CaptureSessionStatus& status = renderer.GetCaptureSessionStatus();
        const bool active = IsActive(status);
        const char* formatNames[] = {
            "PNG (final output)",
            "EXR (linear scene color)",
            "GIF (not available)",
            "MP4 (not available)",
        };

        ImGui::Text("State: %s", GetStateName(status.state));
        ImGui::Text("Frames: accepted %llu, saved %llu, dropped %llu",
                    static_cast<unsigned long long>(status.acceptedFrameCount),
                    static_cast<unsigned long long>(status.savedFrameCount),
                    static_cast<unsigned long long>(status.droppedFrameCount));
        if (!status.lastOutputPath.empty())
        {
            ImGui::TextWrapped("Last output: %s", status.lastOutputPath.string().c_str());
        }
        if (!status.error.empty())
        {
            ImGui::TextWrapped("Error: %s", status.error.c_str());
        }
        if (!state.message.empty())
        {
            ImGui::TextWrapped("%s", state.message.c_str());
        }

        ImGui::BeginDisabled(active);
        ImGui::InputText("Output directory", &state.outputDirectory);
        ImGui::InputText("Base name", &state.baseName);
        ImGui::Combo("Format", &state.outputFormat, formatNames, IM_ARRAYSIZE(formatNames));
        const CaptureSessionOutputFormat outputFormat = static_cast<CaptureSessionOutputFormat>(state.outputFormat);
        ImGui::TextDisabled(outputFormat == CaptureSessionOutputFormat::Exr ?
                                "Source: pre-tone-map scene color" :
                                "Source: final output");

        ImGui::Checkbox("Use ROI", &state.useRegion);
        if (state.useRegion)
        {
            ImGui::InputInt("ROI X", &state.regionX);
            ImGui::InputInt("ROI Y", &state.regionY);
            ImGui::InputInt("ROI Width", &state.regionWidth);
            ImGui::InputInt("ROI Height", &state.regionHeight);
        }
        ImGui::InputInt("FPS", &state.framesPerSecond);
        ImGui::InputInt("Warmup frames", &state.warmupFrames);
        ImGui::Checkbox("Frame limit", &state.useFrameLimit);
        if (state.useFrameLimit)
        {
            ImGui::InputInt("Frames", &state.frameLimit);
        }
        ImGui::Checkbox("Duration limit", &state.useDurationLimit);
        if (state.useDurationLimit)
        {
            ImGui::InputFloat("Duration (seconds)", &state.durationSeconds, 0.1f, 1.0f, "%.2f");
        }
        ImGui::Checkbox("Fixed-step clock", &state.fixedStep);
        if (ImGui::Button("Start Capture Session"))
        {
            std::string error;
            if (renderer.StartCaptureSession(BuildConfig(state), error))
            {
                state.message = "Capture session started.";
            }
            else
            {
                state.message = "Unable to start capture session: " + error;
            }
        }
        ImGui::EndDisabled();

        ImGui::SameLine();
        ImGui::BeginDisabled(!active);
        if (ImGui::Button("Stop Capture Session"))
        {
            renderer.StopCaptureSession();
            state.message = "Capture session is draining queued output.";
        }
        ImGui::EndDisabled();

    }
} // namespace RtPbrSurvey
