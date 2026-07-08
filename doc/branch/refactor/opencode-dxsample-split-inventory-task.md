# OpenCode Task: DXSample Split Inventory

## Objective

Prepare the split of the DirectX-Graphics-Samples application framework code.

This task is an **inventory and plan task**, not a broad implementation task. The goal is to classify the responsibilities currently gathered around `DXSample`, `Win32Application`, `SampleApp`, and `D3D12HelloTexture`, then recommend the first small safe split.

The end goal is a standalone `RtPbrSurvey` repository with clear ownership boundaries:

```txt
App/
Platform/
Engine/
Shared/
```

Do not move large code blocks yet unless the change is obviously tiny and low risk.

## Workspace

```txt
C:\work\DirectX-Graphics-Samples\Samples\Desktop\D3D12HelloWorld\src\RtPbrSurvey
```

## Background

`DXSampleHelper.h` function extraction and include removal are complete or nearly complete. `DXSampleHelper.h` may remain as a compatibility shim, but it should no longer drive ownership decisions.

The next major dependency knot is `DXSample`.

`DXSample` is Sample framework code. Some of it is still useful, but its responsibilities need to be separated into explicit owners:

* Platform: Win32, command line, paths, timing, window title, process integration.
* App: sample lifecycle, mode selection, scene/UI orchestration.
* Engine/RHI: rendering device connection, debug layer/logging integration, frame execution.
* Shared: small non-platform utilities that are not app-specific.

## Primary Files To Inspect

Start with:

```txt
DXSample.h
DXSample.cpp
Win32Application.h
Win32Application.cpp
SampleApp.h
SampleApp.cpp
D3D12HelloTexture.h
D3D12HelloTexture.cpp
Main.cpp
GraphicsDevice.h
GraphicsDevice.cpp
Platform/
Shared/
Renderer/
```

Also inspect project registration if needed:

```txt
RtPbrSurvey.vcxproj
RtPbrSurvey.vcxproj.filters
```

## Questions To Answer

Answer these with file and line references where possible:

1. What does `DXSample` currently own?
   * Command line parsing
   * Window title/name
   * Asset path
   * Width/height/aspect ratio
   * Logging options
   * Debug-layer log polling
   * Frame statistics
   * Lifecycle hooks
   * Any rendering-facing state

2. Which responsibilities are truly Platform-owned?

3. Which responsibilities are App-owned?

4. Which responsibilities are Engine/RHI-owned?

5. Which responsibilities are only compatibility leftovers from the Microsoft sample framework?

6. What is the smallest safe first split?

## Desired Output

Create or update a concise markdown report:

```txt
doc\branch\refactor\dxsample-split-inventory.md
```

The report should contain:

```md
# DXSample Split Inventory

## Summary

## Current Ownership Map

| Responsibility | Current File(s) | Current Owner | Target Owner | Notes |
|---|---|---|---|---|

## Dependency Notes

## Recommended First Split

## Candidate Work Units

## Risks

## Verification Needed For Implementation
```

## Recommended Classification

Use this target model unless the code suggests a better split:

### Platform

Examples:

* Window creation and message loop.
* Command line argument parsing.
* Process-level file path discovery.
* Wall-clock or high-resolution timer access.
* OS-specific debug/log output.

Possible future names:

```txt
Platform/CommandLineOptions.h
Platform/ApplicationHost.h
Platform/WindowInfo.h
Platform/Time.h
```

### App

Examples:

* `HelloTexture` app lifecycle.
* Scene selection.
* UI orchestration.
* App mode transitions.
* Per-sample behavior.

Possible future names:

```txt
App/RtPbrSurveyApp.h
App/AppSettings.h
App/DebugUiController.h
```

### Engine / RHI

Examples:

* D3D12 device-facing debug configuration.
* Renderer frame execution.
* GPU resource ownership.
* RHI debug message collection if it depends on D3D12 interfaces.

Possible future names:

```txt
Engine/Rhi/Dx12/Dx12DebugLog.h
Engine/Rhi/Dx12/Dx12DeviceContext.h
```

### Shared

Examples:

* Small cross-cutting non-platform utilities.
* Error helpers.
* D3D12 helper headers if they are intentionally shared by DX12/RHI code.

## Suggested First Split Candidates

Evaluate these and recommend one:

### Candidate A: Extract command line options

Move command-line parsing data out of `DXSample` into a small value object, for example:

```txt
Platform/CommandLineOptions.h
Platform/CommandLineOptions.cpp
```

This is likely a good first split if parsing is mostly self-contained.

### Candidate B: Extract debug log configuration

Move log-related options and debug-layer log polling boundaries into a smaller owner.

This is useful if `DXSample` currently mixes app lifecycle and D3D12 debug reporting.

### Candidate C: Extract window/app description

Move window title, width, height, and aspect-ratio data into a neutral app/platform settings object.

This is useful if many files query `DXSample` only for simple window metadata.

### Candidate D: Defer implementation

If all splits are risky, do only the inventory and explain the first safe follow-up task.

## Out Of Scope

Do not do these in this task:

* Do not rewrite the application framework.
* Do not move `D3D12HelloTexture` into `App/`.
* Do not split `Win32Application` yet.
* Do not change runtime behavior.
* Do not alter rendering output.
* Do not delete `DXSample`.
* Do not commit, push, merge, reset, checkout, or switch branches.

## If You Make Code Changes

Code changes are optional and should only happen if the first split is tiny.

If code changes are made:

1. Keep them very small.
2. Update `RtPbrSurvey.vcxproj` and `.filters` if files are added.
3. Update `doc\branch\refactor\engine-separation.md` with a commit-unit note.
4. Preserve CRLF line endings.
5. Do not reorder unrelated includes.

## Verification

Always run:

```powershell
git diff --check
```

If code changed, run Debug x64 build:

```powershell
& "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\amd64\MSBuild.exe" RtPbrSurvey.vcxproj /p:Configuration=Debug /p:Platform=x64 /m
```

If no code changed, build is optional. Say whether it was skipped.

## Expected Final Report

In the OpenCode report, include:

* Files inspected.
* Files changed, if any.
* Current `DXSample` responsibility map.
* Recommended first split.
* Why the recommendation is safe.
* Risks or blockers.
* `git diff --check` result.
* Build result, if run.
* Final status: `done` or `blocked`.

