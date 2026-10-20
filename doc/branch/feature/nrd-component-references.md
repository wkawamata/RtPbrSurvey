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

## Paired-reference study

### Paired-reference acquisition policy

Runner: `Tests/PathTracing/qualify_nrd_component_references.py`.
The planned reference cohort is Total/Diffuse/Specular, seeds 11 and 29,
8192 samples each. The 14 existing ROI masks and pair-NRMSE limit 0.03 are
unchanged. A failed variance qualification is inconclusive, not an NRD defect.
Pure-metal Diffuse must be exactly zero and is classified as inactive; other
dark components are inconclusive rather than divided by a near-zero RMS.

A preliminary 16-spp acceleration probe at 256 samples was rejected. Its Total
capture differed from the validated 1-spp capture by a maximum of 0.02685546875
and global NRMSE 0.0003484692584, exceeding the predeclared per-value numerical
budget. Matching camera, seed and sample-count metadata does not establish
pathwise equivalence. The accumulation texture is FP32, but the cause of this
difference is not established. No transport or acceptance threshold was changed
to admit it, and the 16-spp image is not accepted reference evidence.
Local failed report: `bin/PathTracingValidation/nrd-component-pairs-20261020/report.json`.

The replacement acquisition retains the validated one-spp CLI sample-target
path. Before high-sample acquisition it replays all five 256-sample components,
requiring bit identity to the accepted plumbing cohort and rechecking energy
closure. It also revalidates the historical reference chain, executable/shader
hashes, camera, transport state and NRD-disabled state. This does not relax the
separate one-spp NRD input admission contract.

### Results

Local report:
`bin/PathTracingValidation/nrd-component-pairs-20261020-one-spp/report.json`.
Status: `completed-reference-pair-study`; verdict: `partially-inconclusive`.
Eleven fresh captures completed: five bit-identical 256-sample replays, followed
by six 8192-sample references. All capture/log identities, fixture and artifact
hashes were checked. D3D12 errors: 0. Python tests: 171/171; targeted history and
SDK adapter CTests: 2/2. No renderer rebuild or rendering-setting change was
needed in this unit.

| Component | Qualified pair | Exact-zero inactive | Inconclusive |
| --- | ---: | ---: | ---: |
| Total | 12 | 0 | 2 |
| Diffuse | 5 | 7 | 2 |
| Specular | 10 | 0 | 4 |

The following pairs exceed the unchanged 3% reference-NRMSE criterion:

| Component | Fixed ROI | Pair NRMSE |
| --- | --- | ---: |
| Total | dielectric-smooth/dark | 3.7240% |
| Total | dielectric-smooth/mirror-proxy | 4.7736% |
| Diffuse | dielectric-smooth/dark | 3.8919% |
| Diffuse | dielectric-smooth/mirror-proxy | 10.0104% |
| Specular | dielectric-rough/dark | 3.6023% |
| Specular | dielectric-smooth/border | 3.9114% |
| Specular | dielectric-smooth/dark | 4.4930% |
| Specular | dielectric-smooth/mirror-proxy | 4.2619% |

In particular, the focus mirror-proxy ROI does not yet have a qualified
component reference. Its Diffuse pair disagreement cannot establish either
an NRD bug or the absolute attribution of the historical total-color bias.
The mirror-proxy mask remains an analytic selection proxy, not specular-only
ground truth. The earlier qualified 32768-sample Total references remain
separate evidence; this study does not replace them with unqualified 8192-sample
images. No NRD quality verdict, default-on promotion or equal-work claim follows
from this reference qualification.

SHA-256 provenance:

- Report: `566efb7d18b2cc58e3836f2c6a95bf6c8b3900c20363687cb9b4e444f02a737c`.
- Reference executable: `9036129401f6336da789124a130c5c205a43e8c7e51c339c6592acff941a2f68`.
- PT shader: `bfab1bfca68586e917612fa30fa9ba70ac656761774a2ff7f692ba34ee935c56`.

## Remaining Step 2 work

- The 27 qualified active pairs and seven exact-zero inactive pairs have now
  been compared against the seed-7 control/diffuse-5 output cohorts. See
  `nrd-component-comparison.md` for the limited mean-bias and cancellation
  results; this does not resolve the excluded mirror-proxy reference.
- Increase reference sampling for the eight inconclusive pairs and requalify;
  do not relax the 3% limit or assume a larger sample count must converge.
  A guarded 32768-sample runner is implemented; its first long acquisition
  ended without a capture, so qualification remains pending. See
  `nrd-component-refinement.md` for the failed attempt and successful short smoke.
- Extend restored-component comparisons to newly qualified references and
  additional output seeds; preserve contribution weights and cancellation.
- Separate estimator/input variance, guide/admission defects, material factor
  defects and filtering response. Keep unknown causes unknown.
- Keep the pre-pass defaults and default-off policy unchanged until the scene,
  temporal and human acceptance work supports a deliberate decision.
