# Physical component reference acquisition

## Step 2, acquisition plumbing (2026-10-20)

Purpose: obtain converged references for each physical lobe, rather than infer
per-lobe bias from a total-color reference. This unit validates acquisition only;
the real-scene bug/input-quality/filter-limit classification remains open.

The SDK diagnostic build accepts:

```text
-PathTracingReferenceComponent diffuse|specular|stable|background
```

This process-wide automation option selects the contribution accumulated into
the existing PT accumulation and SceneColor textures. It is not a new material
model or a GUI/preset output mode. Ray sampling, full path evaluation and the
ordinary per-frame guide signals remain unchanged. The original Radiance mode
continues to accumulate full radiance. No new GPU textures or public SDK types
are introduced. Values 4-7 of the internal shader output selector are reserved
for this diagnostic; public debug-output enum values are unchanged.

Selection must happen before Renderer initialization. Device creation happens
earlier in this app and is not grounds to reject the setting. The diagnostic
component is explicitly recorded in capture metadata. The option is rejected
by builds without NRD SDK diagnostics. It is not persisted in scene settings.
If NRD Preview is requested, component references remain incompatible: the
conversion guard rejects them and presentation must fall back to Native.
The scene-level 1-spp NRD admission contract is not relaxed.

## Accepted verification

Runner: `Tests/PathTracing/validate_nrd_reference_components.py`.
Local report:
`bin/PathTracingValidation/nrd-reference-components-20261020-final/report.json`.
Result: `passed-reference-component-plumbing`; quality `not-evaluated`.

Eight fresh captures:

1. Native/NRD normal-path outputs at seed 7/frame 30: both bit-identical to the
   historical control after revalidating its 18-capture reference chain.
2. Diffuse reference with NRD Preview requested: zero valid NRD inputs and an
   incompatible-input Native fallback, not an NRD quality result.
3. Total/Diffuse/Specular/Stable/Background at seed 11 and 256 samples, matching
   fixed camera, pixel-center sampling, four bounces and environment mode 7.

`-PathTracingSamples` explicitly selects one sample per frame in the existing
application. The final probe uses that policy, not a presumed 16-spp preset.
NRD Preview and distance diagnostics are off for the component references.

Across 2,073,600 pixels the component sum reconstructs Total within the fixed
FP16 capture / FP32 accumulation budget: maximum absolute difference
0.000244140625, maximum bound ratio 0.2716320551. The budget is the sum of one
FP16 ULP per captured term plus
`64*(256+4)*eps32*max(total,sum,1e-8)`; this is energy-closure validation, not a
convergence criterion. Stable is zero in this fixture, so its nonzero path is
not positively exercised here. Diffuse, Specular and Background are nonzero.

D3D12 errors: 0. Repaired incremental Debug build: 0 warnings/errors. NRD Python
tests: 165/165; SDK/CLI/history CTests: 3/3. The first full solution build also
had the existing unused Streamline delay-import linker warnings. Separate build:
`bin/PathTracingValidation/nrd-reference-build`; older app/reference builds are
retained. The new build includes the preexisting uncommitted Scene Editor source
changes; those changes are not part of this unit's commit.

Two preliminary attempts are excluded from accepted evidence: the first hit an
incorrect new initialization guard (device-ready was confused with renderer-
initialized) and timed out; the second stopped because the runner incorrectly
expected 16 spp despite the CLI sample-target policy. Both defects were fixed;
all eight final captures were reacquired separately without relaxing any
energy, camera, sample-count or NRD-admission check.

## Remaining Step 2 work

- Acquire independent seed-11/29 high-sample Total/Diffuse/Specular references
  with this same executable, and qualify reference variance on the fixed ROIs.
- Do not call these 256-sample images converged references. If a component's
  reference does not qualify, increase sampling or report inconclusive.
- Compare restored NRD component outputs against qualified component references;
  record contribution weights and cancellation in total-color errors.
- Separate estimator/input variance, guide/admission defects, material factor
  defects and filtering response. Keep unknown causes unknown.
- Keep the pre-pass defaults and default-off policy unchanged until the scene,
  temporal and human acceptance work supports a deliberate decision.
