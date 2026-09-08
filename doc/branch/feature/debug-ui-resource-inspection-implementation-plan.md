# Debug UI and Resource Inspection Implementation Plan

## Status

Incremental implementation plan derived from the DLSS, Hybrid Reflection, RenderGraph, Preview, and Depth visualization requests.

Each stage must remain buildable and reviewable. Runtime rendering settings are shared across UI modes, and diagnostic features must not alter normal RenderGraph ownership or DLSS SR/RR results.

## Part 1: Operator UI and Existing Workflow Cleanup

This part changes UI presentation while reusing existing renderer settings and the current single Preview path.

### 1.1 Simple and Detail Views

- [x] Add a reusable `Simple | Detail` segmented control.
- [x] Add independent DLSS SR and RR Simple/Detail modes with two shared status rows.
- [x] Preserve the current DLSS controls in Detail mode.
- [x] Add Hybrid Reflection Simple mode with exactly the three requested checkboxes.
- [x] Make Hybrid Reflection Simple the initial default and preserve the current Detail controls.
- [x] Apply equivalent Simple/Detail behavior to the application and runtime-owned Debug UI paths.

### 1.2 Existing Preview UI Cleanup

- [x] Rename visible `Open Preview` buttons to `Preview`.
- [x] Wrap DLSS and RR input choices after three radio/button items in each group.
- [x] Persist DLSS SR, DLSS RR, and Hybrid Reflection mode choices as independent local UI preferences.
- [ ] Perform visual QA for narrow and normal Debug window widths.
- [x] Keep `DLSS Input Debug` independently expandable from the SR/RR display modes.

## Part 2: Multiple Preview Foundation

This part replaces the single effective Preview output with bounded independent slots.

### 2.1 Inspector Manager

- [x] Focus an existing window when the same resource is requested.
- [x] Open a new slot when a different resource is requested.
- [x] Support close, close-all, and a maximum of four live slots.
- [x] Preserve independent semantic, filter, channel, exposure, scale, and offset settings.

### 2.2 GPU Resources

- [x] Give every live slot an independent output Texture, RTV, SRV, and RenderGraph pass.
- [x] Keep source and output resource transitions owned by RenderGraph.
- [x] Retire closed slot resources through the existing GPU fence path and reuse bounded descriptor slots.
- [ ] Validate resize and DLSS/RR enable changes with multiple windows open.

### 2.3 Window Placement

- [x] Keep the current position for the first new Preview window.
- [x] Arrange unsaved windows from the upper-right in reverse-N order and provide a manual Arrange action.
- [x] Restore user-selected positions and sizes by stable window identity.

## Part 3: Common Depth Visualization

This part improves immediate RR and GBuffer diagnosis before adding node thumbnails.

### 3.1 Depth Mapping

- [x] Add projection-aware Perspective and Orthographic device-depth linearization.
- [x] Add Raw Device, Linear View, and Log View modes.
- [x] Add Display Near/Far, Gamma, Invert, and Reset controls.
- [x] Make Log View the useful default for large camera ranges.

### 3.2 Path Integration

- [x] Apply the same mapping to full-screen Depth views and Preview windows.
- [x] Keep nearest-neighbor sampling for Depth, Motion Vector, and DLSS input diagnostics.
- [ ] Validate clear depth, camera range changes, resize, SR render size, and RR Depth input.

## Part 4: RenderGraph Resource Interaction and Node Presentation

This part connects renderer-owned inspection metadata to the read-only graph.

### 4.1 Resource Actions

- [x] Add the inspectable resource registry and unsupported reasons.
- [x] Open/focus Texture inspectors on double-click; keep registered Buffer dispatch isolated for Part 5.
- [x] Add Preview, Pin Preview, Copy Resource Name, Close Preview, and Index Color context actions.
- [x] Replace `Preview LightPass` with selected resource and active Preview count status.

### 4.2 Node Layout

- [x] Preserve Compact mode.
- [x] Add UE-style stable input pins on the left and output pins on the right.
- [x] Persist positions independently for each layout.

### 4.3 Node Identity and Technology Metadata

- [x] Add the separate small index-color square with gray as the ordinary default.
- [x] Persist user color overrides and support Reset.
- [x] Label the stable `TemporalUpscalerPass` identity as `DLSS SR`.
- [x] Label the stable `DlssRayReconstructionPass` identity as `DLSS Ray Reconstruction`.
- [x] Use NVIDIA green as the DLSS node index-color default.
- [x] Reuse existing Streamline plugin and NGX diagnostics in node details.

### 4.4 Validation

- [x] Pass Debug x64 compilation, inspector tests, and an automated startup/capture smoke test.
- [ ] Visually validate Compact/UE-style switching, stable pin placement, double-click, context actions, and color persistence.
- [ ] Visually validate DLSS SR/RR labels and version metadata with the corresponding nodes enabled.

## Part 5: Rich Diagnostics

This part depends on bounded Preview slots, resource metadata, and the common Depth mapping.

### 5.1 Node Thumbnails

- [x] Add fixed-size Texture thumbnail regions without changing node geometry or pin positions.
- [x] Reuse live Preview slot output for open-resource thumbnails every frame.
- [x] Add four fixed-size hidden thumbnail slots for selected and visible unopened resources.
- [x] Update selected resources every frame and rotate visible unselected resources at an eight-frame interval.
- [x] Apply semantic conversion and the common Depth mapping through the existing Preview output.
- [ ] Visually validate thumbnail content, placeholder states, off-screen filtering, and stable pin positions.

### 5.2 Buffer Inspector

- [x] Show metadata and pass usages for unknown/raw Buffers without guessing a schema.
- [x] Define an explicit 2D element-layout contract for image-like Buffer data.
- [x] Add a dedicated Buffer-to-image GPU conversion path with Image and Heatmap modes.
- [x] Register `MaterialBuffer` as a 16 x 16 reference view for roughness, metallic, occlusion, and AO.
- [ ] Visually validate `MaterialBuffer` Image/Heatmap Preview and Buffer-node thumbnails.
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
