# Editable Path Tracing validation scenes

The four fixtures under `Assets/Scenes/PathTracingValidation/` are ordinary schema-1 SceneDocuments,
with no external assets or scene-specific C++ code. In Scene Editor, enter the scene JSON path in
`Load Scene File` and click `Load`. Nodes, transforms, materials and camera remain editable.
Use `Save As` for personal variants and the editor's render-preset save controls for lighting/render settings.
Keep the original fixtures unchanged when reproducing the recorded measurements.

| Scene directory | Target and expected relationship | Limits |
| --- | --- | --- |
| constant-environment | Neutral dielectric plane, unit constant environment; modes 1/2/5 estimate the same integral at two bounces. | Metallic=0 retains F0=0.04 specular and Fresnel-weighted diffuse. Total output is not `albedo * radiance`. No pure Lambert material is available. |
| single-light-visibility | White point light and floor; between blocker reduces the center ROI, beyond-light blocker leaves it unchanged. Removing the blocker and comparing light heights 2/4 checks the attenuation ratio. | One bounce, environment/emission off. Does not test Spot lights, bias scale robustness or penumbrae. |
| two-surface-indirect | Neutral horizontal plane and red vertical plane; compare one and four bounces for indirect contribution and red bleeding. | Finite sample observation, no analytic full-path expected value. Convergence and significance belong to Part 2. |
| roughness | Three equal-color metallic spheres at roughness 0.18/0.4/0.8 in constant environment; compare GGX sampling convergence later. | No universal ordering of variance asserted. Single center ROI does not measure all three spheres; Part 2 must use separate sphere ROIs. |

Every scene saves its own camera and relative `render-preset.json`. Presets explicitly disable emission,
Russian roulette, automatic exposure and skybox lighting visibility, and specify environment mode,
bounce count, accumulation and samples/frame. Constant lighting uses PT's constant mode and `iblIntensity=1`,
not the procedural map. The procedural environment is also neutral, with its directional/fill/color terms off.
The cyan miss background visible in PNG is presentation only; the floor ROI excludes it.
PNG is used only for composition. PFM is little-endian float RGB, sample-normalized, before ToneMap;
the existing reader converts bottom-up rows to top-left ROI coordinates.

## Reproduction

Build Debug x64 from the repository root, then use Python 3.10+:

```powershell
python -B Tests/PathTracing/validate_part1.py --samples 4
```

Use an absolute Python executable if `python` is not on PATH. Artifacts, derived scene/preset variants,
commands, hashes, diagnostics, means, failures and checks are saved under
`bin/PathTracingValidation/part1/`. The source assets are never modified by the runner.
Do not run concurrent invocations into the same output directory. Use `--output` to retain separate runs.

The resolution must be 1920x1080; a different capture size fails the runner. Static ROI is
`(952,532,16,16)`, seed 7, four samples in the smoke run, one sample/frame. Constant environment repeats
the fixed seed and checks seed 8 variation. All HDR pixels are checked for finiteness by the reused PFM reader.
Process failure, timeout, missing diagnostics or D3D12 error/corruption is retained as a failed run in JSON.
Two existing buffer InitialState warnings may occur and are counted, not treated as errors.

Thresholds are saved in `validation-plan.json` before measurement: unblocked mean >0.001,
blocked/unblocked <0.8, beyond maximum absolute HDR difference <=1e-6, attenuation relative error <=2%.
The attenuation reference is independently calculated as
`f(d,R) = max(0,1-(d/R)^4)^2/d^2`, comparing `f(2,20)/f(4,20)`.
The light is centered above the plane; view/material stay fixed. A small central ROI permits the 2%
spatial approximation already used by `Test-PointLightFalloff.ps1`; this is an approximate ratio check,
not absolute BRDF validation or an independent implementation of the complete integrator.

The four-sample mode observations establish a runnable fixture only. No sampling superiority or
convergence conclusion is drawn from them. Part 2 uses independent reference seeds and larger sample counts.
