# Capture Session UI Phase

This phase depends on Capture Session core PR #78 (`7a5230d`). It adds the reusable `Runtime/CaptureSessionUi` panel without changing the core scheduler contract.

## Reusable panel

`CaptureSessionUiState` owns editable UI values. `CaptureSessionUi::Draw` renders the panel and uses `CaptureSessionUi::BuildConfig` to create the same `CaptureSessionConfig` used by CLI and host APIs. The core `Start` validation remains the authority for all errors.

The panel exposes:

- PNG final output and EXR linear pre-tone-map scene color.
- Full-frame or numeric ROI.
- FPS, warmup frames, frame limit, duration limit, and real-time/fixed-step clock selection.
- Start/Stop, state, accepted/saved/dropped counters, last output path, and error text.

GIF and MP4 remain selectable only as visibly unavailable future formats. Pressing Start routes to the core and reports its explicit unsupported-format error. A later encoder phase can enable those enum values without changing the panel contract.

Mouse-driven ROI selection is intentionally out of scope for this phase.

## Host integration example

The host updates capture scheduling once per frame with its own timing source, then draws the reusable ImGui panel. A physics host should provide its actual simulation clock and check fixed-step readiness before advancing the next simulation step.

```cpp
RtPbrSurvey::CaptureSessionUiState captureUi;

void Tick(double realTimeSeconds, double simulationTimeSeconds, uint64_t renderFrameIndex)
{
    const RtPbrSurvey::CaptureSessionTiming timing = {
        renderFrameIndex,
        realTimeSeconds,
        simulationTimeSeconds,
    };
    RtPbrSurvey::CaptureSessionUi::Update(renderer, timing);

    if (renderer.CanAdvanceCaptureSessionFixedStep())
    {
        AdvancePhysics();
    }

    renderer.RunFrame([&](ID3D12GraphicsCommandList*) {
        ImGui::Begin("Capture");
        RtPbrSurvey::CaptureSessionUi::Draw(renderer, captureUi);
        ImGui::End();
    });
}
```

`CaptureSessionUi::Update` performs no GPU wait. In real-time mode, delayed readback increments the visible dropped counter. In fixed-step mode, a ready request blocks the next advance until it is accepted by the renderer bridge.
