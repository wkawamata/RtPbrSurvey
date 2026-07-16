# Streamline SDK Local Setup Policy

This project keeps NVIDIA Streamline SDK artifacts outside git.

## Scope

The current DLSS work targets DLSS Super Resolution first. DLSS Ray Reconstruction is a later investigation after the reflection signal contract is stable.

DLSS-G / Frame Generation is out of scope. Do not add DLSS-G plugin DLLs, build flags, UI, or documentation as part of the current SR/RR path.

## Local SDK Layout

Place an extracted Streamline SDK release under a user-local, ignored directory:

```text
third_party/
  streamline-sdk-<version>/
    include/
    lib/
    bin/
    docs/
    license.txt
```

The repository ignores `/third_party/streamline-sdk-*/`. Do not commit SDK headers, import libraries, DLLs, plugin DLLs, release archives, or extracted SDK documentation.

## Build Policy

The repository must build without Streamline SDK artifacts. `Renderer/StreamlineAdapter.cpp` remains the only intended source file for future Streamline SDK includes and calls.

Future build integration should be optional and gated by project properties such as:

- `RTPBRSURVEY_STREAMLINE_SDK_DIR`: path to the extracted SDK root.
- `RTPBRSURVEY_HAS_STREAMLINE_SDK`: compile definition set only when the SDK is detected or explicitly enabled.

When the SDK is absent, `QueryStreamlineSupport()` should keep returning `TemporalUpscalerSupportStatus::NotIntegrated`, and the native rendering path remains active.

## Runtime Artifact Policy

Streamline runtime DLLs and DLSS plugin DLLs should be copied to the output directory only from a local SDK installation or build script. They should not be committed to the repository.

Before adding any copy/deploy script, confirm the SDK license and redistribution terms for the exact SDK version and plugin DLLs being used.

## Boundary Rules

- Do not include Streamline headers from `Engine`, `App`, `Scene`, RenderGraph public interfaces, or broad renderer headers.
- Keep SDK types out of repo-owned public interfaces.
- If the integration later moves to a plugin DLL, the plugin should own SDK headers, SDK DLL loading, support queries, and feature evaluation.
- The host should pass only RtPbrSurvey-owned inputs such as D3D12 resources, dimensions, frame constants, settings, and history reset state.

## Next Implementation Step

The next code step should add optional build detection for `RTPBRSURVEY_STREAMLINE_SDK_DIR` while preserving the current SDK-free build behavior.
