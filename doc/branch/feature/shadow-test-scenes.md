# feature/shadow-test-scenes

## RayQuery Shadow Notes

RayQuery shadow validation scenes already exist:

- `Shadow Test: Ground + Cubes`
- `Animated Shadow Grid`
- `Contact Shadow Test`
- `Occluder Wall Test`

Do not add another dedicated shadow scene unless a new failure mode cannot be isolated with these scenes. Prefer improving UI presets, camera defaults, and this comparison checklist so the same scenes remain easy to compare across branches.

## Scene Usage

- `Shadow Test: Ground + Cubes`: primary static comparison scene for shadow direction, bias, normal bias, peter-panning, light size, and soft-shadow stability.
- `Animated Shadow Grid`: moving-object scene for TLAS rebuild timing, `prevWorld`/motion-vector sanity, animated occluders, and pause behavior.
- `Contact Shadow Test`: close-contact scene for acne versus detached-contact tuning.
- `Occluder Wall Test`: blocker/receiver separation scene for missed occluders, back-face culling mistakes, and long-ray behavior.

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
- Moving object: in `Animated Shadow Grid`, confirm animated cube rotation and bounce update the TLAS and ShadowMask each frame.
- Pause behavior: press Space while viewing `Animated Shadow Grid`; cube orientation, TLAS debug, and ShadowMask should freeze without snapping.
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
