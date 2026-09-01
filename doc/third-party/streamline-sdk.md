# NVIDIA DLSS Super Resolution Setup

RtPbrSurvey integrates NVIDIA DLSS Super Resolution through Streamline. The
integration is optional: the project continues to build and use the native
rendering path when the SDK is absent.

This project keeps NVIDIA Streamline SDK artifacts outside git. Do not commit
SDK headers, import libraries, DLLs, release archives, or extracted SDK
documentation.

## Scope

The current integration targets DLSS Super Resolution. DLSS Ray Reconstruction
is a later investigation after the reflection signal contract is stable.

DLSS-G / Frame Generation is out of scope. Do not add DLSS-G plugin DLLs, build
flags, UI, or documentation as part of the current SR/RR path.

## Supported SDK

The default MSBuild and CMake configuration expects NVIDIA Streamline SDK
2.12.0. Download `streamline-sdk-v2.12.0.zip` from the official Streamline
release page:

https://github.com/NVIDIA-RTX/Streamline/releases/tag/v2.12.0

Expected SHA-256:

```text
f5c0a3d870707dddc3570fb4bcd3655cf48a8a68c3a9d342910cfa21b77dcf48
```

Verify the archive before extracting it:

```powershell
Get-FileHash -Algorithm SHA256 .\streamline-sdk-v2.12.0.zip
```

## Local SDK Layout

Extract the contents directly into the following ignored directory. `include`,
`lib`, and `bin` must be immediate children of the SDK root.

```text
third_party/
  streamline-sdk-2.12.0/
    include/
    lib/
    bin/
    docs/
    license.txt
```

The repository ignores `/third_party/streamline-sdk-*/`.

## Build

Build normally after extracting the SDK:

```powershell
msbuild RtPbrSurvey.sln /p:Configuration=Debug /p:Platform=x64
```

MSBuild detects `include\sl.h` and defines
`RTPBRSURVEY_HAS_STREAMLINE_SDK`. It links `sl.interposer.lib` and copies these
runtime files beside `RtPbrSurvey.exe`:

- `sl.interposer.dll`
- `sl.common.dll`
- `sl.dlss.dll`
- `nvngx_dlss.dll`

To use an SDK in another location, set the project property explicitly:

```powershell
msbuild RtPbrSurvey.sln /p:Configuration=Debug /p:Platform=x64 `
  /p:RTPBRSURVEY_STREAMLINE_SDK_DIR=C:\SDKs\streamline-sdk-2.12.0
```

For CMake, pass the equivalent cache variable:

```powershell
cmake -S . -B build `
  -DRTPBRSURVEY_STREAMLINE_SDK_DIR=C:\SDKs\streamline-sdk-2.12.0
cmake --build build --config Debug
```

When the SDK is absent, `QueryStreamlineSupport()` returns
`TemporalUpscalerSupportStatus::NotIntegrated`, DLSS controls are disabled, and
the native rendering path remains active.

## Runtime Use

DLSS requires supported NVIDIA RTX hardware and a compatible current driver.
Start the application, select a scene, and open the `DLSS` section in the debug
UI. Confirm that it shows `Temporal Upscaler: Available`, then:

1. Enable `DLSS Enabled`.
2. Select DLAA, Quality, Balanced, Performance, or Ultra Performance.
3. Leave `DLSS Profile` at `Default` unless validating a specific preset.
4. Use the DLSS input debug views to inspect scene color, depth, and motion
   vectors when diagnosing temporal artifacts.

DLSS is disabled by default. Changing quality mode recreates render-sized
resources and resets temporal history. The final DLSS output remains HDR linear
scene color and is tone-mapped by RtPbrSurvey.

For a repeatable Quality-mode run without UI interaction:

```powershell
.\bin\x64\Debug\RtPbrSurvey.exe `
  -AutoSelectGltfDamagedHelmet `
  -EnableDlssSr `
  -DlssQuality quality
```

Accepted quality names are `dlaa`, `quality`, `balanced`, `performance`, and
`ultra-performance`. `-DlssQuality` implies `-EnableDlssSr`.

## Verification

For a Debug build, verify that all runtime DLLs exist beside the executable:

```powershell
Get-Item .\bin\x64\Debug\RtPbrSurvey.exe, `
  .\bin\x64\Debug\sl.interposer.dll, `
  .\bin\x64\Debug\sl.common.dll, `
  .\bin\x64\Debug\sl.dlss.dll, `
  .\bin\x64\Debug\nvngx_dlss.dll
```

Run the normal D3D12 Debug Layer check from `AGENTS.md`. In the UI, verify at
least DLAA and one non-native quality mode. Check camera motion, disocclusion,
fine geometry, and the transition between modes for temporal instability or
stale resource state errors.

If the UI reports `Runtime missing`, confirm that the four DLLs above are next
to the executable. Other status messages distinguish unsupported adapters,
out-of-date drivers or operating systems, disabled hardware scheduling, and
invalid integration state.

## Runtime Artifact Policy

Streamline runtime DLLs and DLSS plugin DLLs are copied to the output directory
only from a local SDK installation. They must not be committed to the
repository.

Before distributing any runtime artifacts, confirm the SDK license and
redistribution terms for the exact SDK version and plugin DLLs being used.

## Boundary Rules

- `Renderer/StreamlineAdapter.cpp` is the only source file that includes
  Streamline headers and calls the SDK.
- Do not include Streamline headers from `Engine`, `App`, `Scene`, RenderGraph
  public interfaces, or broad renderer headers.
- Keep SDK types out of repo-owned public interfaces.
- If the integration later moves to a plugin DLL, the plugin should own SDK
  headers, SDK DLL loading, support queries, and feature evaluation.
- The host should pass only RtPbrSurvey-owned inputs such as D3D12 resources,
  dimensions, frame constants, settings, and history reset state.

## Current Limits

- This is DLSS Super Resolution only. DLSS Ray Reconstruction and Frame
  Generation are not enabled.
- Runtime image-quality validation still requires supported NVIDIA hardware.
- SDK redistribution terms must be reviewed before shipping the runtime DLLs
  outside a local development build.
