# HDR EXR Capture

## Contract

`ScreenshotRequest` keeps the existing PNG defaults:

```cpp
renderer.RequestScreenshot({L"Screenshots/final.png"});
```

The HDR path is explicit. It copies `LightPass.RenderTarget` before tone mapping and writes a half-float OpenEXR file:

```cpp
RtPbrSurvey::ScreenshotRequest request = {};
request.path = L"Captures/lighting_000042.exr";
request.region = RtPbrSurvey::ScreenshotRegion{128, 64, 1024, 512};
request.outputFormat = RtPbrSurvey::ScreenshotOutputFormat::Exr;
request.source = RtPbrSurvey::ScreenshotCaptureSource::PreToneMapSceneColor;
renderer.RequestScreenshot(std::move(request));
```

`Png` is valid only with `FinalOutput`; it captures the composed final back buffer after ImGui. `Exr` is valid only with `PreToneMapSceneColor`; it captures `LightPass.RenderTarget` and excludes ImGui by construction. Unsupported combinations, invalid ROI bounds, unavailable scene color, and unsupported rendering paths return a failed `ScreenshotResult` rather than changing the active rendering path.

## Color and Resolution

The EXR source is `R16G16B16A16_FLOAT` at render resolution. RGB and alpha are decoded from the source half values and saved as linear scene values. The capture applies no tone mapping, display transfer, HDR10 conversion, paper-white scaling, or exposure compensation. Values above 1.0 remain above 1.0.

The current source is available for forward and deferred rendering. Path tracing does not yet expose the same pre-tone-map resource, so it reports that HDR scene color is unavailable.

## Sequence Naming

Capture requests remain independent; this change does not introduce a recording session or frame scheduler. A caller that submits one request per frame owns numbering. Use a zero-padded, monotonically increasing frame index such as `lighting_000042.exr`, with paths constructed before calling `RequestScreenshot`. A single request is valid and does not require a sequence.

## Dependency and Deployment

The encoder is [TinyEXR](https://github.com/syoyo/tinyexr), version `1.0.13` through the `tinyexr` vcpkg manifest port. Its declared license is BSD-3-Clause. The port is static-only and also restores its `miniz` dependency. No encoder DLL is copied or required at runtime.

CMake locates it with `find_package(tinyexr CONFIG REQUIRED)` and links `unofficial::tinyexr::tinyexr`. The Visual Studio project has `VcpkgEnableManifest` enabled, so restoring the manifest supplies headers and static libraries to MSBuild as well. Re-run `vcpkg install --x-manifest-root=. --triplet x64-windows` after a clean checkout before building either route.

This phase deliberately excludes animated GIF, MP4, capture-session scheduling, and FPS policy. Those need separate output-duration and codec contracts.
