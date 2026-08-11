# SceneRenderer Screenshot Capture

## Purpose

`SceneRenderer` exposes a host-facing screenshot API that captures the final swap-chain back buffer after the ImGui pass and before presentation. The capture path is independent of `DebugDumpCapture`, which remains an internal renderer diagnostic for HDR gradient validation.

## Public API

```cpp
renderer.RequestScreenshot({outputPath});

// Poll after subsequent RunFrame calls.
while (std::optional<RtPbrSurvey::ScreenshotResult> result = renderer.ConsumeScreenshotResult())
{
    if (result->succeeded)
    {
        // result->path is the absolute output path.
    }
    else
    {
        // result->error describes the failure.
    }
}
```

Requests are queued and one screenshot is recorded per frame. `ConsumeScreenshotResult()` is non-blocking. It returns no value until the GPU fence for the captured frame has completed and PNG encoding has finished.

## Ownership

Runtime and renderer own:

- final-back-buffer copy after ImGui
- D3D12 readback resources and GPU fence lifetime
- `R10G10B10A2_UNORM` unpacking
- HDR10 PQ/Rec.2020 to displayable SDR/sRGB conversion
- PNG encoding and parent-directory creation
- success, failure, dimensions, and absolute output path reporting

The host owns:

- capture controls and keyboard shortcuts
- output directory and filename policy
- automation such as capture-after-frames and exit-after-capture
- presentation of `ScreenshotResult` to the user

The standalone application demonstrates this boundary with its Debug UI `Screenshot` section. TankPhysicsSandbox can use the same API without depending on the standalone App layer.

## Capture Ordering

The frame graph order is:

1. scene rendering and tone mapping
2. ImGui
3. screenshot copy, only when a request is queued
4. transition back to the default `PRESENT` state
5. command submission and present

This guarantees that host-injected ImGui windows are present in the captured PNG.

## Color Conversion

The swap chain uses `DXGI_FORMAT_R10G10B10A2_UNORM`.

- SDR output is already sRGB encoded by the tone-map pass and is quantized to RGBA8.
- HDR10 output is decoded from ST.2084 PQ to absolute luminance, converted from Rec.2020 to Rec.709, normalized to the captured paper-white setting, and encoded as sRGB.

The PNG is intentionally displayable SDR. It is not an HDR archival format.

## Validation

`RtPbrSurvey.ScreenshotTests` verifies:

- `R10G10B10A2_UNORM` to RGBA8 unpacking
- PNG encoder success
- PNG signature
- IHDR width and height

An integration check should capture a standalone frame with the Debug window visible, inspect the reported absolute path, and open the image to confirm that both scene content and ImGui are present.
