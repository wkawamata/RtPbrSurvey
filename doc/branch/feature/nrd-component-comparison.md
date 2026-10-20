# Qualified NRD Component Comparison

## Scope

This is the next Step 2 diagnostic after `nrd-component-references.md`.
Runner: `Tests/PathTracing/compare_nrd_component_references.py`.
No renderer, shader, preset, admission rule or NRD default was changed.

The study reuses seed-7 captures acquired on 2026-10-18. It does not present
them as new GPU runs. Direct inputs are 29 control and 20 diffuse-5 component
captures, plus the 11 reference/replay captures acquired on 2026-10-20. The
historical bridge and reference chains are also revalidated. Frozen report
hashes identify these reviewed cohorts; the runner intentionally does not
accept arbitrary replacement reports merely because their counts match.

Captured data/log hashes, metadata, fixture contents, commands, applied pre-pass
profile, camera, sampling and runtime history state are checked. The existing
CPU composition check must reconstruct the actual GPU output; the original
split/admission arithmetic and control-to-variant attribution are rechecked.
The qualified-reference rows are recomputed from the raw reference images.
No ROI masks are reselected. Existing evidence files are not modified.

## Policy

- Profiles: control (Diffuse/Specular pre-pass radii 30/50) and diffuse-5 (5/50).
- Checkpoints: 30 and 120 elapsed frames, static camera, seed 7, one spp.
- NRD's history cap remains 30. This is not 120-sample Native equivalence.
- Reference: independent seeds 11/29 at 8192 samples, physical contribution
  accumulation, pixel-center primary sampling, four bounces, environment mode 7.
- Only the 27 qualified active component/ROI pairs receive a mean-bias result.
- Seven exact-zero metal-Diffuse pairs receive a separate exact-zero output
  check. Eight unqualified pairs are explicitly excluded, not passed.
- Mean RGB bias is `length(mean(output-reference))/length(mean(reference))`.
  The 5% target is a project diagnostic target, not an SDK guarantee.
- Output NRMSE is descriptive. No accumulated Native lobe RMSE comparison is
  available in this cohort, so this is not a complete quality verdict.

## Results

Report: `bin/PathTracingValidation/nrd-component-comparison-20261020-verified/report.json`.
Status: `completed-qualified-component-comparison`; quality: `not-promoted`.
Report SHA-256: `1edb9bbe652b83c1e6e4d2ba8938d4b5e53307144e0227b5630799e764adfe3f`.

All four profile/checkpoint combinations have the same inventory:

| Component | Within mean-bias target | Inactive zero verified | Excluded |
| --- | ---: | ---: | ---: |
| Total | 12 | 0 | 2 |
| Diffuse | 5 | 7 | 2 |
| Specular | 10 | 0 | 4 |

Across 168 result rows: 108 qualified active assessments are within 5%, 28
inactive assessments are exactly zero, and 32 assessments are excluded because
their component references did not qualify. There are no inactive energy leaks.

Maximum mean RGB bias among the eligible ROIs:

| Profile | Frames | Total | Diffuse | Specular |
| --- | ---: | ---: | ---: | ---: |
| control | 30 | 3.9473% | 2.4889% | 4.8751% |
| control | 120 | 3.5681% | 2.1853% | 4.2228% |
| diffuse-5 | 30 | 3.9473% | 2.0087% | 4.8751% |
| diffuse-5 | 120 | 3.5681% | 1.7369% | 4.2228% |

The largest eligible bias is the dielectric-rough highlight's Specular output,
not the historically failing mirror-proxy ROI. In 99 of 108 eligible assessments
all three mean-error channels are negative. This is descriptive darkening in
this cohort, not proof of a particular defect or an NRD performance ceiling.

### Contribution and Cancellation

Ten ROIs have qualified Total and both component references, counting the
verified inactive Diffuse cases. This yields 40 joint descriptions across the
four configurations; 16 other joint descriptions remain excluded. Stable and
Background contributions must be exactly zero on these admitted surface ROIs.

The report records reference linear-luminance fractions and
`length(diffuseMeanError+specularMeanError) /
(length(diffuseMeanError)+length(specularMeanError))`.
A ratio of one means no vector cancellation; a lower ratio indicates partial
cancellation. This is not a new acceptance threshold or a quality promotion.

The minimum ratio is 0.55698 at diffuse-5/frame 30/dielectric-smooth/highlight.
Its reference luminance fractions are approximately 12.74% Diffuse and 87.26%
Specular. Each component also independently remains within the mean-bias target.
Reference mean-closure residuals are retained separately (maximum absolute
channel 2.66346e-6), rather than silently assigning captured-reference
quantization/closure differences to filtering response.

## Validation and Limits

NRD Python tests: 178/178. The seven new tests cover bias direction, unqualified
reference exclusion, inactive energy leaks, malformed output, duplicate/missing
case inventories, missing/inconsistent numerical evidence and cancellation.
Revalidated capture logs contain no D3D12 errors. Fresh GPU runs: zero.
Two preliminary offline runs were superseded by the final runner with
joint descriptions and capture-folder fixture checks; neither added GPU data.

Reference-pair agreement is not a confidence interval on mean bias. These are
point estimates, and results near 5% are not a statistical guarantee of meeting
that target on further seeds or a higher-sample reference.

This is a static single-output-seed study, not a five-seed lobe confirmation,
temporal regression or a new human review. It uses historically bridged output
builds and the newer reference-component build, not one fresh common-build
capture campaign. The prior normal-path bit-identity bridge is retained; no
broader cross-build equivalence is inferred.

The previously problematic dielectric-smooth mirror-proxy reference is still
unqualified for Total, Diffuse and Specular at 8192 samples. Its historical
total-color bias is not resolved or attributed by this study. Earlier qualified
32768-sample Total evidence remains valid and separate. Do not promote the
diffuse-5 profile or default-on policy from these selected results.

## Next

1. Increase sampling for the eight unqualified reference pairs and requalify
   them without changing masks or limits. Prioritize the mirror-proxy region.
2. Extend component comparisons to those pairs only after qualification.
3. Confirm any proposed filtering change across output seeds, temporal cases
   and human review before changing defaults.
