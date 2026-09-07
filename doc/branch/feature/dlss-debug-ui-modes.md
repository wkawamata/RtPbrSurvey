# DLSS Debug UI Modes

## Status

Draft specification for a compact operator view and the existing detailed DLSS diagnostics view.

## Goals

- Keep all current DLSS SR/RR diagnostics and tuning controls available.
- Add a Simple mode for routine enable, quality selection, and status confirmation.
- Share one renderer settings state between Simple and Detail modes.
- Make native RR state distinguishable from copy fallback behavior.

## Mode Selection

The DLSS/RR Debug window provides independent segmented mode selectors for `DLSS SR` and `DLSS RR`.

- `DLSS SR: Simple | Detail` controls SR settings and diagnostics.
- `DLSS RR: Simple | Detail` controls RR settings and diagnostics.
- `DLSS Input Debug` is an independent expandable section shared by both modes.
- Each `Detail` view preserves the current UI and behavior for that feature.
- Each `Simple` view shows only the controls defined below for that feature.
- Switching modes never enables, disables, or resets a renderer feature.
- The two selected display modes are independent local UI preferences and are persisted across application launches.
- Both initial defaults remain `Detail` so existing diagnostics remain immediately available.

## Simple Mode

The first two rows of the DLSS section are always status rows:

1. `Temporal Upscaler: <status>`
2. `DLSS Ray Reconstruction: <status>`

Each row uses the existing status text and may include a short unavailable reason. It must remain visible even when the associated control is disabled.

The status rows are followed by the independent SR and RR mode sections.

SR Simple contains:

- `DLSS`: on/off checkbox for DLSS Super Resolution.
- `DLSS SR Mode`: quality-mode combo containing DLAA, Quality, Balanced, Performance, and Ultra Performance.

RR Simple contains:

- `DLSS RR`: on/off checkbox for native DLSS Ray Reconstruction.

Simple-mode semantics:

- `DLSS` maps to Temporal Upscaler enabled with the Streamline DLSS backend selected.
- `DLSS SR Mode` maps directly to the shared `TemporalUpscalerQualityMode` value.
- `DLSS RR` maps to both the RR render-graph path and guarded native evaluation. A checked control means native RR was requested; copy fallback must not be presented as DLSS RR being active.
- The RR checkbox is disabled when RR support or minimum input readiness is unavailable, while the reason remains visible in the RR status row.
- If native evaluation fails after being requested, keep the requested value visible and report the failure/output source in the RR status row. Do not silently claim that fallback output is native RR.
- Changing SR quality uses the existing deferred render-dimension resize path.

Recommended Simple layout:

```text
Temporal Upscaler: Available
DLSS Ray Reconstruction: Available, Last Evaluate eOk, Native Output
---------------------------------------------------------------
DLSS SR  [ Simple | Detail ]
[x] DLSS
DLSS SR Mode  [ Quality                 v ]
DLSS RR  [ Simple | Detail ]
[x] DLSS RR
```

## Detail Mode

SR Detail retains the current SR sections and controls, including:

- SR Streamline plugin / NGX versions;
- DLSS profile diagnostics and selection;
- jitter X/Y scale and current jitter/index values;
- motion-vector scale and offset controls.

RR Detail retains the current RR sections and controls, including:

- backend, support, raw result, readiness, last evaluate, and output-source diagnostics;
- RR Streamline plugin / NGX versions;
- experimental/native and fallback diagnostics.

Detail controls edit the same settings shown in Simple mode. No duplicated SR or RR settings are allowed.

## Independent Input Debug

`DLSS Input Debug` remains available regardless of the SR and RR Simple/Detail selections. It contains:

- a shared/SR group for Output, Scene Color, Depth, Motion Vectors, Normal, and Albedo;
- an RR additional-input group for Noisy Radiance, Specular Albedo, Roughness, and Specular Hit Distance;
- fixed three-items-per-row radio/button layout within each group;
- `Preview` actions through the shared Preview manager.

RR-only items are disabled when RR input resources are unavailable. Collapsing or expanding this section does not change rendering settings, the active full-screen debug view, or open Preview windows.

## Version Display

Simple mode does not need a full version table. Detail mode remains the authoritative diagnostic view for:

- Streamline version;
- DLSS SR plugin version;
- DLSS SR NGX version;
- DLSS RR plugin version;
- DLSS RR NGX version;
- unavailable/error state for each query.

The RenderGraph may reuse these cached diagnostics for `DLSS SR` and `DLSS Ray Reconstruction` node metadata. It must not query Streamline independently every frame.

## Work Units

### DU-01 Shared View State

- Add independent SR and RR Simple/Detail segmented selectors.
- Persist only the two selected view modes as UI state.
- Verify mode changes do not alter renderer settings.

### DU-02 Simple Controls

- Add the two fixed status rows.
- Bind DLSS enable and quality mode to existing SR settings.
- Bind the single RR control to the existing guarded native RR request path.
- Preserve honest fallback/error reporting.

### DU-03 Presentation Diagnostics

- Expose cached SR/RR version text to RenderGraph presentation metadata.
- Label graph nodes explicitly as `DLSS SR` and `DLSS Ray Reconstruction`.
- Keep stable pass identities unchanged.

### DU-04 Independent Input Debug

- Move shared/SR and RR-only input selection outside both Detail bodies.
- Add an independent expandable `DLSS Input Debug` section.
- Keep separate three-items-per-row subgroups for shared/SR and RR-only resources.

## Acceptance Criteria

- Detail mode remains functionally equivalent to the current DLSS/RR Debug UI.
- Simple mode starts with Temporal Upscaler and RR status rows.
- Simple mode exposes only DLSS enable, SR quality mode, and native RR enable as primary controls.
- Switching either mode preserves every SR/RR setting and current runtime state.
- A copy fallback is never labeled as active native DLSS RR.
- Unsupported controls are disabled with a visible reason.
- Input Debug remains independently expandable in every SR/RR mode combination.
- Version diagnostics appear in Detail mode and can be reused by RenderGraph nodes.
- Debug x64 build succeeds and the D3D12 Debug Layer reports no new errors.
