# Debug UI and Resource Inspection Implementation Plan

## Status

Incremental implementation plan derived from the DLSS, Hybrid Reflection, RenderGraph, Preview, and Depth visualization requests.

Each stage must remain buildable and reviewable. Runtime rendering settings are shared across UI modes, and diagnostic features must not alter normal RenderGraph ownership or DLSS SR/RR results.

## Part 1: Operator UI and Existing Workflow Cleanup

This part changes UI presentation while reusing existing renderer settings and the current single Preview path.

### 1.1 Simple and Detail Views

- [x] Add a reusable `Simple | Detail` segmented control.
- [x] Add DLSS Simple mode with two status rows, DLSS enable, SR mode, and native RR enable.
- [x] Preserve the current DLSS controls in Detail mode.
- [x] Add Hybrid Reflection Simple mode with exactly the three requested checkboxes.
- [x] Make Hybrid Reflection Simple the initial default and preserve the current Detail controls.
- [x] Apply equivalent Simple/Detail behavior to the application and runtime-owned Debug UI paths.

### 1.2 Existing Preview UI Cleanup

- [x] Rename visible `Open Preview` buttons to `Preview`.
- [x] Wrap RR input choices after three radio/button items.
- [ ] Persist Simple/Detail mode choices as local UI preferences.
- [ ] Perform visual QA for narrow and normal Debug window widths.

## Part 2: Multiple Preview Foundation

This part replaces the single effective Preview output with bounded independent slots.

### 2.1 Inspector Manager

- [ ] Focus an existing window when the same resource is requested.
- [ ] Open a new slot when a different resource is requested.
- [ ] Support close, close-all, and a maximum of four live slots.
- [ ] Preserve independent semantic, filter, channel, exposure, scale, and offset settings.

### 2.2 GPU Resources

- [ ] Give every live slot an independent output Texture, RTV, SRV, and RenderGraph pass.
- [ ] Keep source and output resource transitions owned by RenderGraph.
- [ ] Retire slot resources and descriptors after relevant GPU work completes.
- [ ] Validate resize and DLSS/RR enable changes with multiple windows open.

### 2.3 Window Placement

- [ ] Keep the current position for the first new Preview window.
- [ ] Cascade additional unsaved windows by a small deterministic offset.
- [ ] Restore user-selected positions and sizes by stable window identity.

## Part 3: Common Depth Visualization

This part improves immediate RR and GBuffer diagnosis before adding node thumbnails.

### 3.1 Depth Mapping

- [ ] Add projection-aware Perspective and Orthographic device-depth linearization.
- [ ] Add Raw Device, Linear View, and Log View modes.
- [ ] Add Display Near/Far, Gamma, Invert, and Reset controls.
- [ ] Make Log View the useful default for large camera ranges.

### 3.2 Path Integration

- [ ] Apply the same mapping to full-screen Depth views and Preview windows.
- [ ] Keep nearest-neighbor sampling for Depth, Motion Vector, and DLSS input diagnostics.
- [ ] Validate clear depth, camera range changes, resize, SR render size, and RR Depth input.

## Part 4: RenderGraph Resource Interaction and Node Presentation

This part connects renderer-owned inspection metadata to the read-only graph.

### 4.1 Resource Actions

- [ ] Add the inspectable resource registry and unsupported reasons.
- [ ] Open/focus Texture and registered Buffer inspectors on double-click.
- [ ] Add Preview, Pin Preview, Copy Resource Name, Close Preview, and Index Color context actions.
- [ ] Replace `Preview LightPass` with selected resource and active Preview count status.

### 4.2 Node Layout

- [ ] Preserve Compact mode.
- [ ] Add UE-style stable input pins on the left and output pins on the right.
- [ ] Persist positions independently for each layout.

### 4.3 Node Identity and Technology Metadata

- [ ] Add the separate small index-color square with gray as the ordinary default.
- [ ] Persist user color overrides and support Reset.
- [ ] Label the stable `TemporalUpscalerPass` identity as `DLSS SR`.
- [ ] Label the stable `DlssRayReconstructionPass` identity as `DLSS Ray Reconstruction`.
- [ ] Use NVIDIA green as the DLSS node index-color default.
- [ ] Reuse cached Streamline plugin and NGX versions in node details.

## Part 5: Rich Diagnostics

This part depends on bounded Preview slots, resource metadata, and the common Depth mapping.

### 5.1 Node Thumbnails

- [ ] Add fixed-size Texture thumbnails without changing node geometry or pin positions.
- [ ] Update selected/open resources every frame and throttle other visible thumbnails.
- [ ] Apply semantic conversion and the common Depth mapping.

### 5.2 Buffer Inspector

- [ ] Show metadata for unknown/raw Buffers without guessing a schema.
- [ ] Add registered structured summaries, counters, histograms, and heatmaps.

### 5.3 Information Overlay

- [ ] Add an optional lower-left Information window independent from the Debug window.
- [ ] Show the currently selected diagnostic resource and concise renderer/DLSS state.
- [ ] Allow normal ImGui movement and placement persistence.

## Validation Policy

Every implementation commit should pass Debug x64 compilation. Parts that modify GPU resources, descriptors, barriers, or shaders additionally require:

- full Debug x64 link;
- D3D12 Debug Layer run with zero new errors;
- DLSS enable/disable and resize checks;
- RR native/fallback checks when a supported adapter is available.
