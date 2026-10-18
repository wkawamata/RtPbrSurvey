# Static glossy reflection quality

Four opaque spheres use dielectric/metal crossed with roughness 0.6/0.1. Panel
positions match scene 15. A bright procedural softbox over a dark background
creates curved, high-contrast reflected features; no external assets are needed.
The preset must use Environment Map / Importance MIS (mode 7) and Skybox ON.
Constant White / MIS (mode 5) deliberately ignores environment-map illumination;
Skybox OFF uses the fixed clear color for primary misses. These modes are useful
for other tests but invalidate this fixture's requested illumination/control.

```powershell
python Tests/PathTracing/validate_nrd_glossy_quality.py --output bin/PathTracingValidation/nrd-glossy-new --reference-samples 4096 --compare-bounces
```

Use a new/empty output directory and the NRD diagnostic Debug build. The runner
captures five deterministic resource runs after 30 warm-up frames, then two
independent Native references (seeds 11/29, default 4096 spp). Candidate and quality
references both use four bounces. Before references, a sixth
capture disables NRD and zeros every procedural environment color/intensity.
With direct lights and emission disabled, its RGB must be <= 1e-6 everywhere.
Otherwise the runner stops as inconclusive-environment: intended lighting has
not been qualified, and no NRD quality pass/failure is issued. The Native candidate has
accumulated 30 samples; NRD has 30 frames of single-sample history. This is not an
equal-time or equal-total-ray performance comparison.

`--compare-bounces` additionally captures Native references at two and eight
bounces, each with the same two seeds and sample count. Depth agreement uses
whole material interiors: both four/eight-bounce reference pairs must satisfy the
existing 3% pair-NRMSE bound, then their averaged images must differ by at most
3% NRMSE normalized by the eight-bounce reference. Two-bounce differences and
black-pixel fractions are measurements, not a separate quality acceptance gate.
This cannot prove convergence to infinite depth or isolate small reflected-sphere
regions; Cornell Box user observations are separate from this automated fixture.

Material masks use captured albedo with document sRGB8 quantization accounted
for. Three pixels are eroded at silhouettes; all remaining pixels are evaluated,
including any Native fallback. At least 90% must actually use admitted NRD, with
at least 100 interior pixels per material. Background must remain exact Native.

Criteria fixed before measurement:

- Pair-reference NRMSE at most 3%, normalized by reference RGB RMS.
- Reference luminance p95/p5 contrast at least 2.
- NRD RMSE no greater than the accumulated Native candidate RMSE.
- NRD mean RGB-vector bias at most 5% of the reference mean-vector magnitude.

Unconverged references and inadequate contrast are explicitly inconclusive,
not NRD quality passes. A qualified but regressing result is reported as a quality
failure without relaxing limits. Reports retain commands, hashes and metadata.

This measures static image error, not every-frame stability, moving objects,
camera cuts, perfect-mirror transport or human acceptance. It cannot establish
general specular quality or change the experimental/default-off NRD policy.

## Requalifying a complete 4096-spp depth run

```powershell
python Tests/PathTracing/requalify_nrd_glossy.py --source bin/PathTracingValidation/nrd-glossy-depth-20261018 --output bin/PathTracingValidation/nrd-glossy-requalified-new
```

The source must contain the exact twelve-capture depth suite. The loader verifies
capture/log/fixture/helper/executable hashes, commands, runtime metadata, zero
environment control and recomputed quality/depth aggregates. It rejects missing,
duplicate, mismatched or changed evidence instead of trusting a status/count.
Source scene/preset copies are used, not a later edited asset description.

Only depth-four/eight references are recaptured at 8192 spp with seeds 11/29.
Five candidate resources, the zero-environment control and the two 4096-spp
depth-two references are historical reuse. Four/eight comparisons therefore use
matching 8192-spp references; two-to-four remains a diagnostic comparison across
different sample counts. VSync is disabled for these fresh fixed-sample static
references, with the actual argument retained. No timing/performance claim is made.

The source runner did not record shader hashes. Requalification records current
compiled shader hashes for fresh references and explicitly retains that historical
provenance limitation rather than retroactively certifying source shader hashes.

## Local highlight and reflection ROIs

```powershell
python Tests/PathTracing/validate_nrd_glossy_rois.py --source bin/PathTracingValidation/nrd-glossy-requalified-20261018 --output bin/PathTracingValidation/nrd-glossy-rois-new
.\Tests\PathTracing\Start-NrdVisualReview.ps1 -Scene Glossy -Launch
```

ROI analysis rerenders nothing. It revalidates the twelve original and four fresh
reference records, including their hashes, runtime metadata and recomputed source
aggregates. Selection uses only averaged depth-four reference brightness and
scene geometry, never NRD error. Each material has highlight (top 5% luminance),
dark (bottom 20%) and adjacent border (three four-neighbor dilation steps) masks.
Both smooth materials additionally have ideal-mirror reflection proxy masks.
These use analytic spheres, stride two and 80% target radius; they are not actual
GGX sample paths or triangle-hit labels. Source albedo interiors exclude silhouettes.

The fourteen ROIs each require at least 100 pixels, reference-pair NRMSE <= 3%,
NRD RMSE <= Native RMSE and mean RGB-vector bias <= 5%. Dark or unconverged
references are inconclusive. Generated mask PNGs and their hashes are retained.
The contact sheet uses diagnostic exposure 8, Reinhard and sRGB; metrics use
unmodified linear RGB, not this display transform.

The 2026-10-18 run passed twelve ROIs. Smooth dielectric dark and mirror-proxy
references did not qualify (pair NRMSE 3.7240% and 4.7736%). The latter's observed
8.4158% mean bias exceeds the 5% quality limit, but the unconverged reference
prevents either acceptance or a qualified regression verdict. Overall status is
inconclusive-rois (exit 1), not execution failure. No limit was relaxed.

The visual launcher creates three unanswered static-review items: highlights,
reflected spheres and dark regions. It preserves four bounces, mode 7 and Skybox
ON. Human review, Cornell-specific ROIs and temporal glossy behavior remain
separate from this fixed-scene numeric evidence.

## Refining references without moving the ROIs

```powershell
python Tests/PathTracing/refine_nrd_glossy_rois.py --source bin/PathTracingValidation/nrd-glossy-rois-20261018 --output bin/PathTracingValidation/nrd-glossy-refined-new
```

This validates the complete previous evidence chain and recomputes all fourteen
old measurements and masks before acquisition. It then captures two fresh
four-bounce Native references at 32768 spp (seeds 11/29), with NRD disabled and
VSync disabled. The original Native/NRD candidates are reused, not recaptured.
The masks remain those selected from the original 8192-spp reference pair: fresh
brightness or NRD error cannot move the tested regions. The seeds are independent
of each other, but the longer sequences extend the earlier same-seed references;
they are not claimed to be independent of the ROI-selection images.

All fourteen ROIs are remeasured under the unchanged limits, including the twelve
previously passed ROIs. Artifact hashes are checked before and after acquisition;
nonzero exit, timeout, missing evidence or malformed runtime state cannot pass.
The generated comparison.png shows Native/NRD/fresh reference without colored
ROI overlays. The source report retains the actual colored masks and their hashes.
No fresh eight-bounce comparison or general depth-convergence claim is included.

The 2026-10-18 fixed-camera run completed both references with zero D3D12 errors.
All fourteen reference pairs now qualify. Thirteen ROIs pass; smooth dielectric
mirror-proxy fails mean RGB-vector bias (8.3054% > 5%). Its pair NRMSE is 2.6533%,
within 3%, and NRD/Native NRMSE is 24.4777%/35.1154%. Thus noise reduction does not
establish unbiased reflected color. The previously pending dark ROI now passes
(pair 1.9266%, bias 1.2027%). Overall failed-roi-quality (exit 1) is a real local
quality-criterion failure, not an execution failure. It does not establish a root
cause in NRD itself versus our inputs/composition/history; that needs diagnosis.

## Isolating signal arithmetic and history

```powershell
python Tests/PathTracing/isolate_nrd_glossy_bias.py --source bin/PathTracingValidation/nrd-glossy-refined-20261018-fixed-camera --output bin/PathTracingValidation/nrd-glossy-isolation-new
```

The eighteen-capture source chain is revalidated, including both 32768-spp
references and all fixed-mask measurements. A 30-frame seed-7 baseline captures
nineteen resources; Native/NRD color, albedo, admission and composition diagnostics
must reproduce the prior baseline exactly. Independent CPU checks verify final
composition, factor round-trip, unchanged radiance RGB through reconstruction/
admission, and the physical diffuse/specular/stable/background split against the
current frame's radiance. Ten composition resources are then captured at 120
frames, without changing the scene, sample rate or denoiser settings.
The adapter retains the SDK's default 30-frame diffuse/specular linear-history
cap; 120 is elapsed warm-up time, not a 120-frame accumulation-window setting.

The failing fixed reflection ROI is measured at both checkpoints against the same
reference pair. Restored diffuse/specular mean RGB and stable/background means
are reported. There is no converged per-lobe reference: instantaneous input is a
single Monte Carlo sample. A single-seed history comparison cannot establish a
causal SDK defect or a general quality pass. Status completed-diagnostic means the
audit completed, not that the original bias criterion has passed. No fresh
high-sample references, C++/HLSL changes or settings tuning are part of this audit.

The 2026-10-18 audit completed all 29 fresh diagnostic captures with zero D3D12
errors. The 30-frame baseline reproduces exactly; composition CPU/GPU difference
is zero, factor round-trip error <= 1.193e-7, and the current-frame physical split
differs by <= 2.385e-7. Admission/reconstruction preserves radiance RGB exactly.

| Elapsed frames | Mean RGB bias | NRD NRMSE | Native NRMSE | Local quality |
| --- | --- | --- | --- | --- |
| 30 | 8.3054% | 24.4777% | 35.1154% | Failed bias |
| 120 | 5.4107% | 23.5703% | 23.1141% | Failed bias and RMSE |

Both checkpoints use the same qualified reference pair (pair NRMSE 2.6533%).
Stable/background radiance is zero inside the failing ROI. These observations
exclude the audited arithmetic errors, not semantic input errors, sample/history
sensitivity or SDK filtering as a root cause. Repeat fixed-ROI 30/120 comparisons
with independent seeds before adjusting settings or calling the issue resolved.

## Human static review

On 2026-10-18 (Asia/Tokyo), the user accepted all three live visual criteria:
highlights, reflected spheres and dark regions (NRD-LIVE-07/08/09). Local results
are in bin/PathTracingValidation/nrd-visual-20261018-133419-723/results.json.
This fixed-scene subjective pass does not override the numeric ROI failures or
accept moving reflections, Cornell-specific behavior or general scene quality.
