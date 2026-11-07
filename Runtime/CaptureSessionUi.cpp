#include "stdafx.h"

#include "Runtime/CaptureSessionUi.h"

#include "Runtime/SceneRenderer.h"
#include "ImGuiWidgets.h"
#include "Platform/OpenOutputDirectory.h"

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
                case CaptureSessionState::Delaying:
                    return "Start delay";
                case CaptureSessionState::Warmup:
                    return "Warmup";
                case CaptureSessionState::Recording:
                    return "Recording";
                case CaptureSessionState::Draining:
                    return "Draining";
                case CaptureSessionState::Finalizing:
                    return "Finalizing";
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
        config.mp4Bitrate = static_cast<std::uint32_t>((std::clamp)(state.mp4BitrateMbps, 0, 1000)) * 1000000;
        config.gifRepeatMode = static_cast<CaptureSessionGifRepeatMode>(state.gifRepeatMode);
        config.gifRepeatCount = static_cast<std::uint16_t>((std::clamp)(state.gifRepeatCount, 0, 65535));
        config.gifDisposal = static_cast<CaptureSessionGifDisposal>(state.gifDisposal);
        config.warmupFrames = static_cast<std::uint32_t>((std::max)(0, state.warmupFrames));
        config.startDelayMilliseconds = static_cast<std::uint32_t>((std::max)(0, state.startDelayMilliseconds));
        config.frameLimit = state.useFrameLimit ? static_cast<std::uint64_t>((std::max)(0, state.frameLimit)) : 1;
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
        return status.state == CaptureSessionState::Delaying || status.state == CaptureSessionState::Warmup ||
               status.state == CaptureSessionState::Recording ||
               status.state == CaptureSessionState::Draining || status.state == CaptureSessionState::Finalizing;
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
        const auto frame = renderer.GetUiFrameContext();
        const CaptureSessionUiAction action = Draw(renderer.GetCaptureSessionStatus(), state, nullptr, false, false,
                                                   frame.outputWidth, frame.outputHeight);
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
                                                  const char* startBlockedReason,
                                                  bool showSingleCapture, bool singleCaptureBlocked,
                                                  std::uint32_t outputWidth, std::uint32_t outputHeight)
    {
        CaptureSessionUiAction action = CaptureSessionUiAction::None;
        const bool active = IsActive(status);
        const char* formatNames[] = {
            "PNG (final output)",
            "EXR (linear scene color)",
            "GIF (animated final output)",
            "MP4 (H.264 final output)",
        };

        // Keep commands ahead of all variable-height settings and status output.
        ImGui::BeginDisabled(active || startBlockedReason != nullptr);
        if (ImGui::Button("Start Capture Session"))
        {
            action = CaptureSessionUiAction::Start;
        }
        ImGui::EndDisabled();
        if (showSingleCapture)
        {
            ImGui::SameLine();
            ImGui::BeginDisabled(active || singleCaptureBlocked);
            if (ImGui::Button("Capture PNG"))
            {
                action = CaptureSessionUiAction::CapturePng;
            }
            ImGui::EndDisabled();
        }
        const float stopButtonWidth = ImGui::CalcTextSize("Stop Capture Session").x +
                                      ImGui::GetStyle().FramePadding.x * 2.0f;
        if (ImGui::GetContentRegionAvail().x >= stopButtonWidth + ImGui::GetStyle().ItemSpacing.x)
        {
            ImGui::SameLine();
        }
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
                ImGuiWidgets::SliderIntWithControls("Additional repeats", &state.gifRepeatCount, 0, 65535, 1, 1);
            }
            const char* disposalNames[] = {"Keep frame", "Restore background", "Restore previous"};
            int disposalIndex = state.gifDisposal - static_cast<int>(CaptureSessionGifDisposal::Keep);
            if (ImGui::Combo("Frame disposal", &disposalIndex, disposalNames, IM_ARRAYSIZE(disposalNames)))
            {
                state.gifDisposal = disposalIndex + static_cast<int>(CaptureSessionGifDisposal::Keep);
            }
            ImGui::TextDisabled("Frame delay follows FPS (GIF resolution: centiseconds).");
        }
        if (outputFormat == CaptureSessionOutputFormat::Mp4)
        {
            ImGuiWidgets::SliderIntWithControls("Bitrate (Mbps)", &state.mp4BitrateMbps, 1, 100, 1, 12);
            ImGui::TextDisabled("H.264, no audio. Bitrate: 1-100 Mbps; FPS: 1-240.");
            ImGui::TextWrapped("Odd ROI dimensions are padded at the right/bottom edge by one pixel.");
        }

        ImGui::Checkbox("Use ROI", &state.useRegion);
        if (ImGui::Button("Select ROI with mouse"))
        {
            state.selectingRegion = true;
            state.draggingRegion = false;
        }
        if (state.useRegion)
        {
            const int width = (std::max)(1, static_cast<int>(outputWidth));
            const int height = (std::max)(1, static_cast<int>(outputHeight));
            ImGui::BeginDisabled(outputWidth == 0 || outputHeight == 0);
            if (ImGui::BeginTable("RoiCoordinates", 2, ImGuiTableFlags_SizingStretchSame))
            {
                ImGui::TableNextColumn();
                bool regionChanged = ImGuiWidgets::SliderIntWithControls("X", &state.regionX, 0, width - 1, 1, 0, "%d", 0);
                ImGui::TableNextColumn();
                regionChanged |= ImGuiWidgets::SliderIntWithControls("Y", &state.regionY, 0, height - 1, 1, 0, "%d", 0);
                const int maxWidth = width - (std::clamp)(state.regionX, 0, width - 1);
                const int maxHeight = height - (std::clamp)(state.regionY, 0, height - 1);
                ImGui::TableNextColumn();
                regionChanged |= ImGuiWidgets::SliderIntWithControls("W", &state.regionWidth, 1, maxWidth, 1,
                                                                     (std::min)(640, maxWidth), "%d", 0);
                ImGui::TableNextColumn();
                regionChanged |= ImGuiWidgets::SliderIntWithControls("H", &state.regionHeight, 1, maxHeight, 1,
                                                                     (std::min)(480, maxHeight), "%d", 0);
                if (regionChanged)
                {
                    state.regionX = (std::clamp)(state.regionX, 0, width - 1);
                    state.regionY = (std::clamp)(state.regionY, 0, height - 1);
                    state.regionWidth = (std::clamp)(state.regionWidth, 1, maxWidth);
                    state.regionHeight = (std::clamp)(state.regionHeight, 1, maxHeight);
                }
                ImGui::EndTable();
            }
            ImGui::EndDisabled();
            if (ImGui::Checkbox("Show ROI overlay", &state.showRegionOverlay) && !state.showRegionOverlay)
            {
                CancelRegionSelection(state);
            }
        }
        ImGuiWidgets::SliderIntWithControls("FPS", &state.framesPerSecond, 1, 240, 1, 60);
        ImGuiWidgets::SliderIntWithControls("Start delay (ms)", &state.startDelayMilliseconds, 0, 60000, 100, 0);
        ImGuiWidgets::SliderIntWithControls("Warmup frames", &state.warmupFrames, 0, 10000, 1, 0);
        ImGui::Checkbox("Frame limit", &state.useFrameLimit);
        if (state.useFrameLimit)
        {
            ImGuiWidgets::SliderIntWithControls("Frames", &state.frameLimit, 1, 100000, 1, 60);
        }
        else
        {
            ImGui::TextUnformatted("Frames: 1");
        }
        ImGui::Checkbox("Duration limit", &state.useDurationLimit);
        if (state.useDurationLimit)
        {
            ImGuiWidgets::SliderFloatWithControls("Duration (seconds)", &state.durationSeconds,
                                                   0.01f, 3600.0f, 0.1f, 1.0f, "%.2f");
        }
        ImGui::Checkbox("Fixed-step clock", &state.fixedStep);
        ImGui::EndDisabled();

        ImGui::Text("State: %s", GetStateName(status.state));
        ImGui::Text("Frames: accepted %llu, saved %llu, dropped %llu",
                    static_cast<unsigned long long>(status.acceptedFrameCount),
                    static_cast<unsigned long long>(status.savedFrameCount),
                    static_cast<unsigned long long>(status.droppedFrameCount));
        if (static_cast<CaptureSessionOutputFormat>(state.outputFormat) == CaptureSessionOutputFormat::Mp4)
        {
            ImGui::TextWrapped("Reserved/owned: %zu frames, %.1f MiB", status.encoder.outstandingFrames,
                        static_cast<double>(status.encoder.outstandingBytes) / (1024.0 * 1024.0));
            ImGui::TextWrapped("GPU readback: %zu pending, %.1f MiB",
                        status.encoder.gpuPendingFrames,
                        static_cast<double>(status.encoder.gpuReadbackBytes) / (1024.0 * 1024.0));
            ImGui::TextWrapped("Readback allocations/reuses: %llu / %llu",
                        static_cast<unsigned long long>(status.encoder.gpuReadbackAllocations),
                        static_cast<unsigned long long>(status.encoder.gpuReadbackReuses));
            ImGui::Text("Append: %.2f ms (max %.2f), finalize: %.2f ms", status.encoder.lastAppendMilliseconds,
                        status.encoder.maxAppendMilliseconds, status.encoder.finalizeMilliseconds);
        }
        if (!status.lastOutputPath.empty())
        {
            const std::u8string outputPathUtf8 = status.lastOutputPath.u8string();
            const std::string outputPath(outputPathUtf8.begin(), outputPathUtf8.end());
            ImGui::TextUnformatted("Last output:");
            ImGui::SameLine();
            if (ImGui::Button("Open Location Path##session"))
            {
                std::error_code error;
                const std::filesystem::path absolutePath = std::filesystem::absolute(status.lastOutputPath, error);
                state.message = !error && OpenOutputDirectory(absolutePath.parent_path()) ?
                    "Opened output location." : "Unable to open output location.";
            }
            ImGui::TextWrapped("%s", outputPath.c_str());
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
        state.regionLabelHovered = false;
        state.draggingRegionLabel = false;
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
        state.regionLabelHovered = false;
        if (state.selectingRegion || !state.useRegion || !state.showRegionOverlay ||
            !ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            state.draggingRegionLabel = false;
        }
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
                            state.message = "ROI selected. Use Select ROI with mouse to replace it, or edit the numbers.";
                            CancelRegionSelection(state);
                            if (!state.showRegionOverlay)
                            {
                                preview.reset();
                            }
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
            const bool movable = !state.selectingRegion && preview->width <= outputWidth && preview->height <= outputHeight;
            if (movable && state.draggingRegionLabel)
            {
                const ImVec2 mouse = ImGui::GetMousePos();
                const auto moveAxis = [](float position, float grab, float scale, std::uint32_t limit)
                {
                    return static_cast<int>(std::lround(std::clamp((position - grab) / scale, 0.0f,
                                                                  static_cast<float>(limit))));
                };
                if (state.resizingRegionFromBottomRight)
                {
                    const int right = moveAxis(mouse.x - origin.x, state.regionLabelGrabX, xScale, outputWidth);
                    const int bottom = moveAxis(mouse.y - origin.y, state.regionLabelGrabY, yScale, outputHeight);
                    state.regionWidth = (std::max)(1, right - state.regionX);
                    state.regionHeight = (std::max)(1, bottom - state.regionY);
                    preview->width = static_cast<std::uint32_t>(state.regionWidth);
                    preview->height = static_cast<std::uint32_t>(state.regionHeight);
                }
                else
                {
                    state.regionX = moveAxis(mouse.x - origin.x, state.regionLabelGrabX, xScale, outputWidth - preview->width);
                    state.regionY = moveAxis(mouse.y - origin.y, state.regionLabelGrabY, yScale, outputHeight - preview->height);
                }
                preview->x = static_cast<std::uint32_t>(state.regionX);
                preview->y = static_cast<std::uint32_t>(state.regionY);
            }
            const ImVec2 minimum(origin.x + preview->x * xScale, origin.y + preview->y * yScale);
            const ImVec2 maximum(minimum.x + preview->width * xScale, minimum.y + preview->height * yScale);
            ImDrawList* drawList = state.selectingRegion ? ImGui::GetForegroundDrawList() : ImGui::GetBackgroundDrawList();
            drawList->AddRect(minimum, maximum, IM_COL32(255, 196, 0, 255), 0.0f, ImDrawFlags_None, 2.0f);
            char label[96] = {};
            snprintf(label, sizeof(label), "X %u, Y %u", preview->x, preview->y);
            const ImVec2 labelSize = ImGui::CalcTextSize(label);
            const ImVec2 anchor(minimum.x, minimum.y - labelSize.y - 8);
            const ImVec2 labelEnd(origin.x + (std::max)(0.0f, size.x - labelSize.x - 4),
                                  origin.y + (std::max)(0.0f, size.y - labelSize.y - 4));
            const auto bottomRight = [&](ImVec2 p) { return ImVec2(p.x + labelSize.x + 4, p.y + labelSize.y + 4); };
            const ImVec2 labelPosition(std::clamp(anchor.x, origin.x, labelEnd.x),
                                       std::clamp(anchor.y, origin.y, labelEnd.y));
            const ImVec2 handleMin(maximum.x - 5, maximum.y - 5);
            const ImVec2 handleMax(maximum.x + 5, maximum.y + 5);
            const bool canResize = movable && preview->x <= outputWidth - preview->width &&
                preview->y <= outputHeight - preview->height;
            if (movable)
            {
                const bool pointerAvailable = !ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow) && !ImGui::IsAnyItemActive();
                const bool resizeHovered = canResize && pointerAvailable && ImGui::IsMouseHoveringRect(handleMin, handleMax, false);
                state.regionLabelHovered = resizeHovered || (pointerAvailable &&
                    ImGui::IsMouseHoveringRect(labelPosition, bottomRight(labelPosition), false));
                if (canResize)
                {
                    drawList->AddRectFilled(handleMin, handleMax, IM_COL32(255, 196, 0, 255));
                }
                if (state.regionLabelHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                {
                    const ImVec2 mouse = ImGui::GetMousePos();
                    state.regionLabelGrabX = mouse.x - (resizeHovered ? maximum.x : minimum.x);
                    state.regionLabelGrabY = mouse.y - (resizeHovered ? maximum.y : minimum.y);
                    state.resizingRegionFromBottomRight = resizeHovered;
                    state.draggingRegionLabel = true;
                }
                if (state.regionLabelHovered || state.draggingRegionLabel)
                {
                    const bool resize = state.draggingRegionLabel ? state.resizingRegionFromBottomRight : resizeHovered;
                    ImGui::SetMouseCursor(resize ? ImGuiMouseCursor_ResizeNWSE : ImGuiMouseCursor_ResizeAll);
                }
            }
            drawList->AddRectFilled(labelPosition, bottomRight(labelPosition),
                                   IM_COL32(0, 0, 0, 210));
            drawList->AddText(labelPosition, IM_COL32(255, 196, 0, 255), label);
            char dimensions[48] = {};
            snprintf(dimensions, sizeof(dimensions), "%u x %u", preview->width, preview->height);
            const ImVec2 dimensionsSize = ImGui::CalcTextSize(dimensions);
            const ImVec2 dimensionsPosition(
                std::clamp(maximum.x - dimensionsSize.x - 4, origin.x,
                           origin.x + (std::max)(0.0f, size.x - dimensionsSize.x - 4)),
                std::clamp(maximum.y + 4, origin.y,
                           origin.y + (std::max)(0.0f, size.y - dimensionsSize.y - 4)));
            drawList->AddRectFilled(dimensionsPosition,
                ImVec2(dimensionsPosition.x + dimensionsSize.x + 4, dimensionsPosition.y + dimensionsSize.y + 4),
                IM_COL32(0, 0, 0, 210));
            drawList->AddText(dimensionsPosition, IM_COL32(255, 196, 0, 255), dimensions);
        }
    }
} // namespace RtPbrSurvey
