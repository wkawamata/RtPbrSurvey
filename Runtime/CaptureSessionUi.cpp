#include "stdafx.h"

#include "Runtime/CaptureSessionUi.h"

#include "Runtime/SceneRenderer.h"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cmath>

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
        config.outputSubdirectory = state.outputSubdirectory;
        config.baseName = state.baseName;
        config.outputFormat = static_cast<CaptureSessionOutputFormat>(state.outputFormat);
        config.source = config.outputFormat == CaptureSessionOutputFormat::Exr ?
            ScreenshotCaptureSource::PreToneMapSceneColor : ScreenshotCaptureSource::FinalOutput;
        config.clock = state.fixedStep ? CaptureSessionClock::FixedStep : CaptureSessionClock::RealTime;
        config.framesPerSecond = static_cast<std::uint32_t>((std::max)(0, state.framesPerSecond));
        config.gifRepeatMode = static_cast<CaptureSessionGifRepeatMode>(state.gifRepeatMode);
        config.gifRepeatCount = static_cast<std::uint16_t>((std::clamp)(state.gifRepeatCount, 0, 65535));
        config.gifDisposal = static_cast<CaptureSessionGifDisposal>(state.gifDisposal);
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
        const CaptureSessionUiAction action = Draw(renderer.GetCaptureSessionStatus(), state);
        if (action == CaptureSessionUiAction::Start)
        {
            std::string error;
            state.message = renderer.StartCaptureSession(BuildConfig(state), error) ?
                "Capture session started." : "Unable to start capture session: " + error;
        }
        else if (action == CaptureSessionUiAction::Stop)
        {
            renderer.StopCaptureSession();
            state.message = "Capture session is draining queued output.";
        }
    }

    CaptureSessionUiAction CaptureSessionUi::Draw(const CaptureSessionStatus& status, CaptureSessionUiState& state,
                                                  const char* startBlockedReason)
    {
        CaptureSessionUiAction action = CaptureSessionUiAction::None;
        const bool active = IsActive(status);
        const char* formatNames[] = {
            "PNG (final output)",
            "EXR (linear scene color)",
            "GIF (animated final output)",
            "MP4 (not available)",
        };

        // Keep commands ahead of all variable-height settings and status output.
        ImGui::BeginDisabled(active || startBlockedReason != nullptr);
        if (ImGui::Button("Start Capture Session"))
        {
            action = CaptureSessionUiAction::Start;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!active);
        if (ImGui::Button("Stop Capture Session"))
        {
            action = CaptureSessionUiAction::Stop;
        }
        ImGui::EndDisabled();
        if (startBlockedReason != nullptr)
        {
            ImGui::TextWrapped("%s", startBlockedReason);
        }

        ImGui::BeginDisabled(active);
        ImGui::InputText("Output directory", &state.outputDirectory);
        ImGui::InputText("Subfolder", &state.outputSubdirectory);
        ImGui::InputText("Base name", &state.baseName);
        ImGui::Combo("Format", &state.outputFormat, formatNames, IM_ARRAYSIZE(formatNames));
        const CaptureSessionOutputFormat outputFormat = static_cast<CaptureSessionOutputFormat>(state.outputFormat);
        ImGui::TextDisabled(outputFormat == CaptureSessionOutputFormat::Exr ?
                                "Source: pre-tone-map scene color" :
                                "Source: final output");
        if (outputFormat == CaptureSessionOutputFormat::Gif)
        {
            ImGui::SeparatorText("Animated GIF");
            const char* repeatNames[] = {"Play once", "Loop forever", "Repeat count"};
            ImGui::Combo("Repeat", &state.gifRepeatMode, repeatNames, IM_ARRAYSIZE(repeatNames));
            if (static_cast<CaptureSessionGifRepeatMode>(state.gifRepeatMode) == CaptureSessionGifRepeatMode::Count)
            {
                ImGui::InputInt("Additional repeats", &state.gifRepeatCount);
            }
            const char* disposalNames[] = {"Keep frame", "Restore background", "Restore previous"};
            int disposalIndex = state.gifDisposal - static_cast<int>(CaptureSessionGifDisposal::Keep);
            if (ImGui::Combo("Frame disposal", &disposalIndex, disposalNames, IM_ARRAYSIZE(disposalNames)))
            {
                state.gifDisposal = disposalIndex + static_cast<int>(CaptureSessionGifDisposal::Keep);
            }
            ImGui::TextDisabled("Frame delay follows FPS (GIF resolution: centiseconds).");
        }

        ImGui::Checkbox("Use ROI", &state.useRegion);
        if (ImGui::Button("Select ROI with mouse"))
        {
            state.selectingRegion = true;
            state.draggingRegion = false;
        }
        if (state.useRegion)
        {
            ImGui::InputInt("ROI X", &state.regionX);
            ImGui::InputInt("ROI Y", &state.regionY);
            ImGui::InputInt("ROI Width", &state.regionWidth);
            ImGui::InputInt("ROI Height", &state.regionHeight);
            ImGui::Checkbox("Show ROI overlay", &state.showRegionOverlay);
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
        ImGui::EndDisabled();

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

        return action;
    }

    void CaptureSessionUi::CancelRegionSelection(CaptureSessionUiState& state)
    {
        state.selectingRegion = false;
        state.draggingRegion = false;
    }

    std::optional<ScreenshotRegion> CaptureSessionUi::RegionFromDrag(
        float startX, float startY, float endX, float endY, float displayWidth, float displayHeight,
        std::uint32_t outputWidth, std::uint32_t outputHeight)
    {
        if (displayWidth <= 0 || displayHeight <= 0 || !std::isfinite(displayWidth) || !std::isfinite(displayHeight) ||
            outputWidth == 0 || outputHeight == 0 || startX == endX || startY == endY ||
            !std::isfinite(startX) || !std::isfinite(startY) || !std::isfinite(endX) || !std::isfinite(endY))
        {
            return std::nullopt;
        }
        const double xScale = static_cast<double>(outputWidth) / displayWidth;
        const double yScale = static_cast<double>(outputHeight) / displayHeight;
        const auto left = static_cast<std::uint32_t>(std::floor(std::clamp(
            static_cast<double>((std::min)(startX, endX)) * xScale, 0.0, static_cast<double>(outputWidth))));
        const auto top = static_cast<std::uint32_t>(std::floor(std::clamp(
            static_cast<double>((std::min)(startY, endY)) * yScale, 0.0, static_cast<double>(outputHeight))));
        const auto right = static_cast<std::uint32_t>(std::ceil(std::clamp(
            static_cast<double>((std::max)(startX, endX)) * xScale, 0.0, static_cast<double>(outputWidth))));
        const auto bottom = static_cast<std::uint32_t>(std::ceil(std::clamp(
            static_cast<double>((std::max)(startY, endY)) * yScale, 0.0, static_cast<double>(outputHeight))));
        if (left >= right || top >= bottom)
        {
            return std::nullopt;
        }
        return ScreenshotRegion{left, top, right - left, bottom - top};
    }

    void CaptureSessionUi::DrawRegionOverlay(const CaptureSessionStatus& status, CaptureSessionUiState& state,
                                            std::uint32_t outputWidth, std::uint32_t outputHeight)
    {
        if (IsActive(status))
        {
            CancelRegionSelection(state);
            return;
        }
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        const ImVec2 origin = viewport->Pos;
        const ImVec2 size = viewport->Size;
        if (size.x <= 0 || size.y <= 0 || outputWidth == 0 || outputHeight == 0)
        {
            CancelRegionSelection(state);
            return;
        }

        std::optional<ScreenshotRegion> preview;
        if (state.selectingRegion)
        {
            ImGui::SetNextWindowPos(origin);
            ImGui::SetNextWindowSize(size);
            ImGui::SetNextWindowFocus();
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
            ImGui::Begin("##CaptureRoiSelection", nullptr,
                         ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground |
                         ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoScrollbar);
            ImGui::InvisibleButton("##CaptureRoiCanvas", size);
            ImGui::SetMouseCursor(ImGuiMouseCursor_None);
            const ImVec2 mouse = ImGui::GetIO().MousePos;
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Right) || ImGui::IsKeyPressed(ImGuiKey_Escape))
            {
                CancelRegionSelection(state);
            }
            else
            {
                if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                {
                    state.draggingRegion = true;
                    state.regionDragStartX = mouse.x - origin.x;
                    state.regionDragStartY = mouse.y - origin.y;
                }
                if (state.draggingRegion)
                {
                    preview = RegionFromDrag(state.regionDragStartX, state.regionDragStartY,
                                             mouse.x - origin.x, mouse.y - origin.y,
                                             size.x, size.y, outputWidth, outputHeight);
                    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
                    {
                        if (preview)
                        {
                            state.regionX = static_cast<int>(preview->x);
                            state.regionY = static_cast<int>(preview->y);
                            state.regionWidth = static_cast<int>(preview->width);
                            state.regionHeight = static_cast<int>(preview->height);
                            state.useRegion = true;
                            state.showRegionOverlay = true;
                            state.message = "ROI selected. Use Select ROI with mouse to replace it, or edit the numbers.";
                            CancelRegionSelection(state);
                        }
                        else
                        {
                            state.draggingRegion = false;
                        }
                    }
                }
            }
            ImGui::End();
            ImGui::PopStyleVar();
            if (state.selectingRegion)
            {
                ImDrawList* drawList = ImGui::GetForegroundDrawList();
                if (mouse.x >= origin.x && mouse.y >= origin.y && mouse.x < origin.x + size.x && mouse.y < origin.y + size.y)
                {
                    drawList->AddLine(ImVec2(mouse.x - 9, mouse.y), ImVec2(mouse.x + 9, mouse.y), IM_COL32(0, 0, 0, 255), 3.0f);
                    drawList->AddLine(ImVec2(mouse.x, mouse.y - 9), ImVec2(mouse.x, mouse.y + 9), IM_COL32(0, 0, 0, 255), 3.0f);
                    drawList->AddLine(ImVec2(mouse.x - 9, mouse.y), ImVec2(mouse.x + 9, mouse.y), IM_COL32(255, 255, 255, 255));
                    drawList->AddLine(ImVec2(mouse.x, mouse.y - 9), ImVec2(mouse.x, mouse.y + 9), IM_COL32(255, 255, 255, 255));
                }
                const ImVec2 hint(origin.x + 16, origin.y + 16);
                const char* text = "Drag to select ROI. Release to apply. Esc / right-click: cancel.";
                const ImVec2 textSize = ImGui::CalcTextSize(text);
                drawList->AddRectFilled(ImVec2(hint.x - 8, hint.y - 8),
                                       ImVec2(hint.x + textSize.x + 8, hint.y + textSize.y + 8),
                                       IM_COL32(0, 0, 0, 210), 4);
                drawList->AddText(hint, IM_COL32(255, 255, 255, 255), text);
            }
        }

        if (!preview && state.useRegion && state.showRegionOverlay && state.regionWidth > 0 && state.regionHeight > 0)
        {
            preview = ScreenshotRegion{static_cast<std::uint32_t>((std::max)(0, state.regionX)),
                                       static_cast<std::uint32_t>((std::max)(0, state.regionY)),
                                       static_cast<std::uint32_t>(state.regionWidth),
                                       static_cast<std::uint32_t>(state.regionHeight)};
        }
        if (preview)
        {
            const float xScale = size.x / static_cast<float>(outputWidth);
            const float yScale = size.y / static_cast<float>(outputHeight);
            const ImVec2 minimum(origin.x + preview->x * xScale, origin.y + preview->y * yScale);
            const ImVec2 maximum(minimum.x + preview->width * xScale, minimum.y + preview->height * yScale);
            ImDrawList* drawList = ImGui::GetForegroundDrawList();
            drawList->AddRect(minimum, maximum, IM_COL32(255, 196, 0, 255), 0.0f, ImDrawFlags_None, 2.0f);
            char label[96] = {};
            snprintf(label, sizeof(label), "%u x %u  (X %u, Y %u)", preview->width, preview->height, preview->x, preview->y);
            const ImVec2 labelSize = ImGui::CalcTextSize(label);
            const ImVec2 labelPosition(std::clamp(minimum.x, origin.x, origin.x + (std::max)(0.0f, size.x - labelSize.x)),
                                       std::clamp(minimum.y - labelSize.y - 8, origin.y, origin.y + (std::max)(0.0f, size.y - labelSize.y)));
            drawList->AddRectFilled(labelPosition, ImVec2(labelPosition.x + labelSize.x + 4, labelPosition.y + labelSize.y + 4),
                                   IM_COL32(0, 0, 0, 210));
            drawList->AddText(labelPosition, IM_COL32(255, 196, 0, 255), label);
        }
    }
} // namespace RtPbrSurvey
