# feature/shadow-test-scenes

## RayQuery Shadow Notes

RayQuery shadow validation scenes already exist:

- `Shadow Test: Ground + Cubes`
- `Shadow Test: Animated Shadow Grid`
- `Shadow Test: Contact Shadow Test`
- `Shadow Test: Occluder Wall Test`

Do not add another dedicated shadow scene unless a new failure mode cannot be isolated with these scenes. Prefer improving UI presets, camera defaults, and this comparison checklist so the same scenes remain easy to compare across branches.

## Scene Usage

- `Shadow Test: Ground + Cubes`: primary static comparison scene for shadow direction, bias, normal bias, peter-panning, light size, and soft-shadow stability.
- `Shadow Test: Animated Shadow Grid`: moving-object scene for TLAS rebuild timing, `prevWorld`/motion-vector sanity, animated occluders, and pause behavior.
- `Shadow Test: Contact Shadow Test`: close-contact scene for acne versus detached-contact tuning.
- `Shadow Test: Occluder Wall Test`: blocker/receiver separation scene for missed occluders, back-face culling mistakes, and long-ray behavior.

Recommended debug view sequence:

1. Use `ShadowMask` first to inspect the binary or softened mask without direct-light shading.
2. Switch back to `Lit` and confirm the direct-light direction matches the mask direction.
3. Use `TlasDebug` if the mask shape looks like the wrong object or a stale transform.

## UI Presets

The `RayQuery Shadow` debug UI provides comparison presets:

- `Hard Ref`: one-sample hard shadow baseline. Use this before tuning soft-shadow settings.
- `Low Bias`: lower normal bias to expose self-intersection acne and contact sensitivity.
- `Soft Compare`: moderate soft shadow for day-to-day comparison.
- `Wide Soft`: larger angular radius and sample count to stress light-size softening and noise.

After applying a preset, adjust individual sliders only for the specific question being tested. Keep screenshots or notes paired with the active scene, render view, preset, and camera position.

## Comparison Checklist

- Bias / normal bias: compare acne on flat receivers against peter-panning around cube feet and sphere contact points.
- Soft shadow: compare `Hard Ref` against `Soft Compare` in `ShadowMask` and `Lit`; the lit edge should soften without flipping direction or losing blockers.
- Light size: increase `Light Angular Radius` and confirm penumbra width grows predictably while hard-shadow contact remains plausible.
- Moving object: in `Shadow Test: Animated Shadow Grid`, confirm animated cube rotation and bounce update the TLAS and ShadowMask each frame.
- Pause behavior: press Space while viewing `Shadow Test: Animated Shadow Grid`; cube orientation, TLAS debug, and ShadowMask should freeze without snapping.
- Back-face culling: cube shadows should not lose faces when viewed from different light/camera angles.
- Ray distance: use `Ray TMax` changes only to isolate far-blocker issues; do not use it as a substitute for fixing transforms or culling.

## Light Direction

`lightDirection` is treated as the surface-to-light direction.

Keep ShadowMask generation and LightPass direct lighting consistent:

- `shaders_RayQueryShadow.hlsl`: `rayDir = normalize(lightDirection)`
- `shaders_LightPass.hlsl`: `lightDir = normalize(lightDirection)`

If one side uses `-lightDirection`, the ShadowMask can look correct by itself while the final lighting looks wrong.

## RayQuery Culling

Do not use `RAY_FLAG_CULL_BACK_FACING_TRIANGLES` for the current shadow ray.

A shadow ray is a binary occlusion test. Back faces are still valid blockers. Enabling back-face culling made cube shadows partially correct and partially missing because different faces were accepted or rejected by winding.

Expected form:

```hlsl
RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> q;
```

## TLAS Instance Transform

Rendering uses `InstanceData::world` directly in shaders. Scene code stores it as `XMMatrixTranspose(M)`.

When filling `D3D12_RAYTRACING_INSTANCE_DESC::Transform`, use the first three rows of `InstanceData::world` directly so TLAS sees the same object-to-world transform convention as the shaders.

The broken version tried to reconstruct the untransposed matrix before filling the DXR 3x4 transform. Translation-only scenes could look acceptable, but animated rotating cubes produced ShadowMask patterns that looked like another cube was projected onto the surface.

## Bias Tuning

Do not tune normal bias first when the mask is structurally wrong.

Bias is for self-intersection acne and peter-panning. If the mask looks like the wrong orientation or another object projection, check light direction, RayQuery culling flags, TLAS transform packing, and TLAS rebuild timing first.

Current baseline:

```text
Normal Bias = 0.01
Ray TMin = 0.001
```

## Animated Pause

Pausing should freeze the current accumulated animation time. Do not multiply the rotation term by a pause speed value.

`Space` toggles scene animation play/pause. Renderer-wide frame pause uses `P`, and `F` advances one frame while renderer-wide pause is active. Keep these controls separate: replacing the scene-animation toggle with frame pause leaves `m_isPlaying` false and prevents animated validation scenes from starting.

Broken pattern:

```cpp
const float speed = context.isPlaying ? 1.0f : 0.0f;
const float rotY = m_accumTime * rotSpeed * speed + phase;
```

Correct pattern:

```cpp
const float rotY = m_accumTime * rotSpeed + phase;
```

`m_accumTime` already stops advancing while paused. Multiplying by zero changes the current orientation back to `phase`.

## Expected Result

- ShadowMask direction matches final direct lighting.
- Cubes do not lose shadow faces due to RayQuery back-face culling.
- Animated cube rotation is reflected in the TLAS / ShadowMask.
- Pressing Space to pause does not change cube orientation.
- All shadow validation scenes look consistent.

## Branch Completion

This branch treats the existing four scenes as the stable RayQuery shadow validation suite. The scene selector groups them under `Shadow Test`, and the `RayQuery Shadow` panel provides reproducible hard, low-bias, and soft-shadow presets. No additional scene is required for the current bias, light-size, occluder, or moving-object coverage.

Final validation requires a successful Debug x64 build and a manual launch through all four scenes. Record visual acceptance separately because shader compilation alone cannot establish shadow quality.

Validation on 2026-08-11:

- MSBuild Debug x64: passed.
- CMake SDK-free Debug build: passed.
- CTest: 9/9 passed.
- Debug executable launch: remained active for six seconds; no D3D12 warning or error was recorded.
- Visual comparison of all four scenes: remains a manual GPU-dependent acceptance step.

Validation on 2026-08-15 after merging current `main`:

- MSBuild Debug x64 and the affected HLSL custom builds: passed with 0 errors and the existing duplicate vcpkg import warning.
- A regression was found in the manual gate: a later temporal-input validation change had reassigned `Space` from `m_isPlaying` to renderer-wide frame pause, so `Animated Shadow Grid` could not start. The branch restores `Space` as scene-animation play/pause and moves renderer-wide frame pause to `P`; `F` remains the paused single-frame step.
- `Shadow Test: Ground + Cubes`: **PASS WITH LIMITATION**. ShadowMask, Lit, and TlasDebug were reviewed. Shadow direction, coverage, and soft edges were usable, but the gray floor and gray cubes have insufficient highlight-side contrast, weakening top-face and acne inspection.
- `Shadow Test: Animated Shadow Grid`: **PASS** after the input fix. Animation start/stop, cube motion, shadow/TLAS tracking, pause/resume without snapping, and camera stability were accepted.
- `Shadow Test: Contact Shadow Test`: **PASS**. Contact continuity, detachment, acne, and camera-stability checks were accepted.
- `Shadow Test: Occluder Wall Test`: **PASS**. Blocker coverage, Lit/ShadowMask direction, wall/receiver placement, and view-angle stability were accepted.
- The manual Debug Layer log contained 0 errors. It contained six occurrences of the same known committed-buffer initial-state warning and no new warning type.

Final scope decision: the suite is accepted for the documented RayQuery shadow checks with the Ground + Cubes contrast limitation retained explicitly. This does not claim that every view or material arrangement is optimal for all future shadow-quality measurements.
