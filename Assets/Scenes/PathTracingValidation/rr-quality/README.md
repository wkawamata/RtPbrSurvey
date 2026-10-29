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
