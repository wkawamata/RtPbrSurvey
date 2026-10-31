# RR material and edge quality

This replaces the uniform-white roughness spheres for subjective RR assessment.
The old roughness fixture remains unchanged for numerical regression tests.

- Textured DamagedHelmet: texture detail, normal-map detail, silhouette and disocclusion.
- Copper sphere: colored panel reflections, specular stability and motion trails.
- Rough floor: contact shadows, indirect color and reflection blur.
- Red/blue panels and thin dark bars: high-contrast boundaries, thin geometry and reflection detail.
- Warm key and cool fill: visible lighting direction and material differences without a uniform-white environment.

Start with 1 spp, 4 bounces, Pixel Center ON, Continuous RR ON.
Compare Native and RR from the same camera. Native Accumulate can provide a settled reference;
it is not an equal-sample or equal-time comparison. NRD requires its supported 1 spp/center profile.

Inspect the helmet scratches, floor contact shadows, bar edges and sphere reflections while
stationary, during a slow lateral camera move, and after stopping. Then repeat at 4 spp and
with Pixel Center OFF (RR frame-wide jitter). Dark edge artifacts remain unresolved until
compared against Native at the same pose; they must not be dismissed as expected shading.

Launch from repository root:

```powershell
.\build\reconstruction-input-validation\Debug\RtPbrSurvey.exe -SceneFile Assets/Scenes/PathTracingValidation/rr-quality/scene.json -PathTracingReconstruction rr -EnableDlssRayReconstruction -EnableExperimentalNativeRayReconstruction -PathTracingRrContinuous
```

Assets reference the existing local DamagedHelmet; no model or texture is duplicated.

## User visual validation (2026-10-28)

The user confirmed stable output both while stationary and during camera movement
for this scene at the initial 1 spp, 4 bounce, Pixel Center ON, continuous RR profile.
The preview also reached verified native RR output with no captured D3D12 errors.
This acceptance does not cover the separate 4 spp/frame-jitter profile or resolve
the previously reported dark edge artifacts in the old three-sphere fixture.

## Step 1 profiles (2026-10-29)

- `render-preset-4spp-center.json`: 4 spp with fixed pixel-center primary.
- `render-preset-4spp-jitter.json`: 4 spp with RR frame-wide jitter.

Pass either file with `-RenderPreset` alongside the scene and RR flags above.
Keep 4 bounces, accumulation and continuous RR enabled in both profiles.
Check helmet scratches, thin bars, silhouettes and colored reflections while
stationary, moving slowly and stopping. Record each profile independently.
Automated rendering checks do not substitute for user visual acceptance.

Automated checks on 2026-10-29 passed for both 4 spp profiles at 1280x720:
continuous verified RR presentation, more than 10 completed outputs, and no
captured D3D12 ERROR/CORRUPTION messages. User visual acceptance is pending.
Local evidence: `build/reconstruction-input-validation/user-test-rr/step1-20261029/report.json`.

User acceptance on 2026-10-29: 4 spp, Pixel Center ON passed both stationary
inspection (helmet detail and thin bars) and slow camera movement/stop
(silhouettes and sphere reflections). Frame-jitter visual acceptance remains pending.

User acceptance on 2026-10-29: 4 spp, Pixel Center OFF (frame-wide RR jitter)
also passed stationary inspection and slow camera movement/stop, with no reported
thin-edge/detail instability or reflection/silhouette trails. Step 1 is complete
for this scene: both 4 spp center and 4 spp jitter profiles passed user inspection.
Native-reference comparison and the remaining evaluation steps are still pending.

## Step 2: same-pose Native reference comparison (2026-10-29)

Use the saved scene camera, lights and 1280x720 output for both images.
Native reference: 256 accumulated samples, pixel center, 4 bounces.
RR: 4 spp, pixel center, 4 bounces, continuous evaluation, capture after 160 loops.
This compares appearance against a settled reference, not equal sample count or equal time.
Inspect dark silhouette halos, helmet scratches, thin-bar coverage, contact shadows,
and colored reflections. A stable but blurred image is not automatically a pass.
The old three-sphere edge issue is a separate unresolved case.
Local captures/logs: `build/reconstruction-input-validation/user-test-rr/step2-20261029/`.
User acceptance is pending.

User acceptance on 2026-10-31: the same-pose Native 256-sample / RR 4 spp
pixel-center comparison passed all five inspected categories: helmet fine detail,
thin-bar coverage, copper reflections/highlights, silhouettes/contact edges, and
floor/wall shadows/reflections. Step 2 appearance comparison is accepted for this
scene and profile. The old three-sphere dark-edge report remains unresolved and
is not covered by this acceptance. Step 3 (fast motion/disocclusion) is pending.
