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
