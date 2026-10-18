# NRD RELAX Input Semantics Audit

Date: 2026-10-18 (Asia/Tokyo)

## Decision

No concrete hard input-contract violation was identified in the reviewed path.
This is not proof that the inputs have optimal statistics, that independently
sampled distance is equally suitable in every scene, or that the measured glossy
darkening is fixed. Do not rewrite Native transport merely to force both distance
guides to use the Native continuation ray: that ray is not automatically an
accepted in-lobe distance for both physical components.

The next bounded experiment is pre-pass spatial-mixing sensitivity, with current
defaults retained as the control. Numeric thresholds and prior failures remain
unchanged. No renderer, shader, SDK configuration or default policy changed in
this audit.

## Reviewed Contract

The authority is the local pinned NRD v4.17.3 source, commit
`792eff196afdd350fd9c3f862119017ccb438a0e`, especially README sections
`NOISY INPUTS`, `MATERIAL DEMODULATION` and `DENOISING COMBINED SIGNALS`,
`Shaders/NRD.hlsli` and `Include/NRDSettings.h`. This audit did not substitute
documentation from a newer SDK.

| Topic | Implementation and observation | Disposition |
| --- | --- | --- |
| Physical split | `Shaders/shaders_PathTracing.hlsl:822` evaluates both BRDF components for the same Native continuation direction and applies per-channel component/total fractions to shared downstream terms. Primary direct/NEE terms use their component BRDFs. | Proposal identity is not used to classify the new physical signals. Prior component-oracle and current composition evidence remain distinct from this source review. |
| Native preservation | `MakeDenoiserDistanceInputs` at line 398 derives separate salted RNG states from pixel/sample index and does not consume the inout Native RNG. | The distance path does not alter Native throughput, PDF or random consumption. Separate RNG alone is not a demonstrated contract violation. |
| In-lobe distance | Diffuse uses cosine sampling; specular uses a view-conditioned GGX visible-normal sampler. `TraceDenoiserDistance` at line 368 traces the closest accepted surface and measures from the unbiased primary surface to the hit. | No primary camera distance, summed multi-bounce path length, or PDF-scaled distance is forwarded. Independent distance/radiance correlation remains a quality question. |
| MIS/NEE distance | Dedicated in-lobe rays are used rather than blindly forwarding the chosen light-sample distance or mixture-proposal distance. | Avoids treating any chosen MIS direction as automatically valid for both lobes; it does not establish optimal signal-distance correlation. |
| Miss/unavailable states | `Shaders/shaders_NrdConversion.hlsl:64` checks explicit traced/miss/inactive/invalid/untraced states. Qualified misses map to 65504 at lines 164-165, not zero. Unavailable states do not become invented finite geometry hits. | Untraced/range-invalid cases are rejected. Invalid directional samples may enter the tested external reconstruction path, then require nonzero reconstructed distance for an active lobe. |
| Material factors | `Shaders/NrdRelaxInputConversion.hlsli:66` calls pinned `NRD_MaterialFactors` with shading normal, reconstructed camera view, diffuse albedo, F0 and the guide roughness. The same stored factors are used in composition. | Runtime round-trip and composition arithmetic already passed Step 10G. This is not a proof of perfect demodulated input statistics. |
| Packing | `RELAX_FrontEnd_PackRadianceAndHitDist` returns radiance RGB and unnormalized distance W after guards. Candidate RGB is preserved through external reconstruction/admission. | No REBLUR normalization or extra PDF weighting is applied to RELAX distance. |
| Reconstruction | `Engine/RtPbrSurveyEngine.cpp:6543` runs the external pinned reconstruction shader. Admission rejects unresolved active zero distances. `Renderer/NrdRelaxSdkAdapter.cpp:129` disables the SDK-internal reconstruction pass. | External reconstruction followed by internal OFF is intentional, not a missing-pass finding. Full arbitrary-scene coverage is not established. |

The physical lobe split and distance policy were introduced in branch steps
2C/2G/2H/2I. This review does not silently replace that policy or relabel legacy
proposal-classified `DiffuseRadianceHitT`/`SpecularRadianceHitT` as physical input.

## Remaining Risks

1. The distance sampler is the documented unbounded Heitz 2018/2019 VNDF, not
   the SDK README's recommended VNDF-v3 baseline. Below-surface samples are
   explicitly invalidated rather than resampled, then reconstructed/admitted or
   rejected. Passing finite sampling vectors is not proof of ideal coverage on
   arbitrary curved, normal-mapped or animated geometry.
2. Both physical components use a mixture-path estimator, while the two distance
   rays are independently sampled. The README requires in-lobe distance, but the
   reviewed text does not establish identical RNG/path samples as a universal
   condition. Conversely, in-lobe sampling alone does not prove this decoupling
   is harmless for small reflected features or temporal tracking.
3. Primary NEE and downstream transport are summed into each physical component.
   Material demodulation is an approximation and cannot guarantee low-variance
   directional radiance. Rare high-energy contributions and limited temporal
   history remain possible sources of filtering bias.
4. Default RELAX diffuse/specular pre-pass radii are 30/50 pixels, with 30-frame
   linear-history caps. Spatial mixing can be significant relative to a small
   reflected sphere. This is a hypothesis to test, not a confirmed cause.
5. Step 10H found all five sampled streams darker in all mean RGB channels in the
   smooth dielectric mirror ROI, with ensemble bias 7.3389%/5.8307% at 30/120
   frames. The arithmetic and human static checks pass within their scopes, but
   neither overrides those numeric failures.

## Fresh Contract Reexecution

Local evidence:
`bin/PathTracingValidation/nrd-input-semantics-audit-20261018/report.json`.
It contains exact commands, exit codes, source/binary/CSO hashes and log hashes.
All recorded artifacts were unchanged after execution.

- Focused CTest: 10/10 passed. Includes adapter contracts, sampler, packing,
  material factors, input conversion and external reconstruction contracts.
- Hardware sampler: 28,672 HLSL samples, 1,539 expected below-surface results,
  maximum direction error 8.62977e-6; no D3D12 errors.
- Hardware RELAX packing: eight exact cases passed; no D3D12 errors.
- Hardware material factors: eight round-trip cases passed; no D3D12 errors.
- Hardware guarded input conversion: eight cases passed; no D3D12 errors.
- Physical-lobe Python tests: 10/10 passed; NRD Python tests: 143/143 passed.
- SDK checkout HEAD was verified as the pinned commit listed above.

Existing Debug executables and CSOs were reused, not rebuilt. These tests
reexecute their compiled contract kernels; they do not prove a new source build,
paired radiance/distance statistics, arbitrary scene inputs or improved image
quality. No fresh scene-quality capture or repeated human review was performed.
The machine status `passed-contract-kernels` is deliberately separate from the
source-review disposition and `qualityVerdict = not-evaluated`.

## Next Experiment

Use one parameter change per variant, declared before capture:

| Variant | Diffuse pre-pass radius | Specular pre-pass radius |
| --- | --- | --- |
| Control | 30 | 50 |
| Specular sensitivity | 30 | 10 |
| Diffuse sensitivity | 5 | 50 |

Keep sampler, physical split, material factors, history caps, scene, camera,
exposure and all fixed ROI masks unchanged. Do not disable all pre-pass filtering
or change both radii at once and then attribute the result to one component.
Start with a fresh seed-7 control/variant comparison, then use the predeclared
five-seed 30/120 profile for any proposed improvement. Report all trials, including
regressions outside the focus ROI. Do not select or move masks using NRD error.

Any new binary must have explicit provenance. Before reusing older high-sample
references, establish and record a Native-output compatibility bridge or acquire
new references; do not bypass the current executable/hash checks silently.
Capture-time arithmetic, admission coverage and deterministic reset checks remain
required. A favorable diagnostic variant is not permission to change defaults;
the existing local quality gates, broader temporal work and human acceptance
remain separate.

## Pre-pass experiment implementation status

The SDK-neutral adapter accepts explicit diffuse/specular pre-pass radii at
initialization. Defaults remain 30/50. Successful initialization exposes the
applied values through its description; negative or nonfinite inputs fail and
leave the adapter uninitialized. Zero is accepted for diagnostic use.

Adapter tests cover both predeclared variants, changed SDK dispatch constants,
invalid values in either field, and restoration of identical control constants
after reinitialization. This is CPU planning coverage, not GPU quality evidence.
CLI/runtime wiring supports `-NrdPrepassProfile control|specular-10|diffuse-5`.
Explicit profiles enable PT and NRD Preview, without changing sampling settings.
The option requires an SDK diagnostic build and must be applied before backend
initialization. Captures expose `nrdPrepass` with the profile, an `applied` flag,
and applied radii when the backend is ready. Initialization/fallback failures
must not be interpreted as successful application of an experimental profile.

The separate experiment build is under the ignored
`bin/PathTracingValidation/nrd-prepass-build` directory. The original `build/`
executable remains the historical reference identity. The alternate build can
include local uncommitted Scene Editor changes; it is not a clean-source release.
The static Native/control compatibility bridge is recorded below. Actual
sensitivity captures remain pending; compilation alone does not qualify old
references for the new executable.

The alternate Native PT CSO hash is
`e0322c9a7fc2511eae8481f97735112897f7326f8e5b5d94a9bbca186a7f4664`,
different from the historical CSO hash. No shader source was changed in this
unit. The hash difference is not waived as proof of equivalent rendering;
fresh Native-output comparisons or new high-sample references are required.

## Static control bridge result (2026-10-18)

Runner: `Tests/PathTracing/validate_nrd_prepass_bridge.py`.
Local report:
`bin/PathTracingValidation/nrd-prepass-bridge-20261018-verified/report.json`.

- Status: `passed-static-control-bridge`.
- Revalidated the historical 18-capture reference chain and 30-capture seed
  study, including commands, hashes, runtime state and recomputed ROI results.
- Acquired 20 fresh captures: Native and NRD scene color, seeds
  7/101/307/701/1009, at 30/120 frames, explicit control profile 30/50.
- All 20 full RGBA images matched their historical counterparts exactly:
  maximum absolute error 0 and changed values 0. D3D12 errors: 0.
- Capture metadata matched apart from the declared additive `nrdPrepass` field.
  Runtime comparison excludes only measured `gpuTimeMs`, measured
  `primarySamplesPerSecond`, and the separately validated capture destination.
  Timing values must remain finite and positive; transport state is not excluded.
- NRD Python regression tests: 147/147 passed, including four bridge tests.

Alternate executable SHA256:
`98daf2f6c6dccf0e943e99cbc155dae2c4253fd62733fb1d849f95f93e312db8`.
Historical executable and Native PT shader remain unchanged. The alternate CSO
hash still differs; this is measured static-output compatibility, not a claim
that arbitrary cameras, scenes or future binaries are equivalent.

An initial acquisition under `nrd-prepass-bridge-20261018` was rejected because
the first runner compared timing and output paths for exact equality. Its one
capture is not accepted evidence. The corrected policy is explicit above and
the verified run reacquired all 20 outputs in a separate directory.

The bridge permits the controlled pre-pass investigation to proceed against
the existing fixed references for this fixture. It does not reverse the previous
failed Glossy ROI quality verdict, change defaults, or replace temporal/human
validation. The next unit is the predeclared 30/10 and 5/50 sensitivity comparison.

## Seed-7 pre-pass sensitivity result (2026-10-18)

Runner: `Tests/PathTracing/validate_nrd_prepass_sensitivity.py`.
Local report:
`bin/PathTracingValidation/nrd-prepass-sensitivity-20261018/report.json`.
Execution completed; quality verdict remains `failed-sampled-rois`.

The runner revalidated all 20 control-bridge captures and their historical
counterparts, plus the 18-capture reference chain. It acquired 18 fresh captures:
three profiles, two checkpoints, and Native/NRD/composition-diagnostic outputs.
All fixed ROI references qualified, with actual NRD composition coverage and no
fallback in the ROIs. D3D12 errors: 0. Native outputs were identical to the
control bridge for every profile, and fresh control NRD outputs also matched.
No shader, transport, history cap, ROI mask or threshold changed.

Focus ROI: `dielectric-smooth/mirror-proxy`, 792 pixels. Values below are percent.

| Diffuse/specular radius | Frames | Mean RGB bias | NRD NRMSE | Native NRMSE | All ROI passes |
| --- | --- | --- | --- | --- | --- |
| 30/50 control | 30 | 8.305409 | 24.477725 | 35.115390 | 13/14 |
| 30/10 | 30 | 8.305409 | 24.477725 | 35.115390 | 13/14 |
| 5/50 | 30 | 5.760504 | 24.207514 | 35.115390 | 13/14 |
| 30/50 control | 120 | 5.410741 | 23.570250 | 23.114135 | 8/14 |
| 30/10 | 120 | 5.410741 | 23.570250 | 23.114135 | 8/14 |
| 5/50 | 120 | 2.649094 | 23.470798 | 23.114135 | 8/14 |

Neither variant changed any ROI's combined pass/fail status in this trial.
Diffuse radius 5 reduced focus mean bias at both checkpoints, but still failed
the 5% bias gate at frame 30 and the Native-relative RMSE gate at frame 120.
Thus it is a sensitivity finding, not a quality fix or a default recommendation.
The specular variant changed the full output image at both checkpoints, despite
identical focus metrics; its lack of focus response is not proof of an inactive
setting. Other ROI measurements, including adverse numeric changes that did not
cross a gate, remain in the report rather than being omitted.

NRD Python regression tests: 150/150 passed. Human acceptance was not repeated
or revoked. The next experiment is the predeclared five-seed 30/120 confirmation
of diffuse radius 5, with all 14 ROIs and regressions retained. Do not promote a
default on the basis of this one-seed experiment.

## Five-seed diffuse-radius confirmation (2026-10-18)

Runner: `Tests/PathTracing/validate_nrd_prepass_seeds.py`.
Accepted local report:
`bin/PathTracingValidation/nrd-prepass-seeds-20261018-fixed-camera/report.json`.
Status: `completed-prepass-seed-study`; quality: `failed-sampled-rois`.

The runner revalidated 68 existing captures (18 reference-chain, 20 bridge,
30 historical control) and recomputed the historical 140 ROI assessments and
summaries. It acquired 30 new diffuse-5 captures across the predeclared five
seeds and 30/120 checkpoints. Native outputs remained exactly equal to Control.
All 140 ROI references qualified and had actual NRD composition without fallback.
D3D12 errors: 0. NRD Python tests: 152/152 passed.

| Checkpoint | Control ROI passes | Diffuse-5 ROI passes | Focus ensemble RGB bias, control -> variant |
| --- | --- | --- | --- |
| 30 frames | 65/70 | 67/70 | 7.338916% -> 4.557598% |
| 120 frames | 46/70 | 48/70 | 5.830717% -> 3.193221% |

Four ROI cases recovered, all in `dielectric-smooth/mirror-proxy`: seed 101
at both checkpoints, seed 307 at 120, and seed 701 at 30. No previously passing
combined ROI verdict regressed. This does not mean every numeric metric improved:
mean bias decreased in 53/140 cases, increased in 17/140 and was unchanged in 70.
Focus NRMSE increased slightly for seed 307 at 30 and seed 701 at 120, without
crossing the Native-relative RMSE gate. All per-case deltas remain in the report.

Focus mean RGB bias percentages, 30 / 120 frames:

| Seed | Control | Diffuse-5 |
| --- | --- | --- |
| 7 | 8.305409 / 5.410741 | 5.760504 / 2.649094 |
| 101 | 6.523830 / 5.599948 | 4.034193 / 3.158916 |
| 307 | 3.599054 / 7.282456 | 0.784276 / 4.711694 |
| 701 | 6.851573 / 2.406798 | 3.978632 / 0.791550 |
| 1009 | 11.538764 / 8.580870 | 8.661021 / 5.843090 |

At 30 frames, focus bias still failed for seeds 7 and 1009. At 120, seed 1009
still failed bias and seed 7 still failed Native-relative RMSE. Other smooth
highlight/border and metal ROI failures remain. The ensemble number is bias of
the ensemble mean RGB, not the average per-seed bias or ensemble image RMSE.
The default 30-frame linear-history cap is unchanged at both checkpoints.

An earlier attempt at `nrd-prepass-seeds-20261018` stopped after 11 acquisitions:
the seed-101/frame-120 NRD capture had camera X=-0.04 and accumulation 98, so it
was rejected by the fixed-state checks. None of that attempt's captures is used
in the accepted report; all 30 were reacquired separately. The origin of the
input/state change was not proven. No capture acceptance rule was relaxed.

Conclusion: diffuse pre-pass radius has a reproducible effect on the measured
bias, but radius 5 is not a complete quality fix. Defaults remain 30/50 and NRD
remains experimental/default-off. No new human acceptance is claimed. Next,
isolate diffuse/specular physical contributions in the fixed reflection ROI to
locate the remaining bias before further tuning or default changes.

## Physical-lobe sensitivity attribution (2026-10-18)

Runner: `Tests/PathTracing/attribute_nrd_prepass_lobes.py`.
Accepted local report:
`bin/PathTracingValidation/nrd-lobe-attribution-20261018-verified/report.json`.
Status: `completed-lobe-sensitivity-attribution`; quality is not promoted.

The diagnostic revalidated the original Control arithmetic/29 captures and the
static bridge evidence, then evaluated 5/50 at seed 7 and frames 30/120. The
accepted report includes ten newly captured frame-120 buffers and ten revalidated
frame-30 buffers from the initial acquisition. Those ten had been fully acquired
before an aggregation bug passed ten resources to a helper requiring exactly
three. The initial attempt did not complete; the revised runner records its
report hash and original runner hash, rechecks all reused commands/hashes/logs/
metadata, and recomputes all assessments. No failed summary is trusted.

The physical components are reconstructed using the captured material factors
and admission masks, including inactive-lobe handling. Their sum is checked
against the actual GPU composite; attribution requires actual NRD coverage in
the fixed ROI and exact Native equality. All 14 ROI totals are also assessed.

At the 792-pixel `dielectric-smooth/mirror-proxy` ROI:

| Frame | Diffuse change in mean physical RGB (5/50 minus 30/50) | Specular maximum change | Maximum delta closure error |
| --- | --- | --- | --- |
| 30 | (2.960546e-5, 9.424768e-5, 6.684612e-5) | 0 | 4.423782e-9 |
| 120 | (2.645709e-5, 1.067510e-4, 7.517842e-5) | 0 | 5.820766e-9 |

Stable and background components are zero and unchanged in this ROI. Specular
restoration is bit-identical per pixel. The output lift caused by reducing the
diffuse pre-pass radius is therefore attributable to the diffuse component in
this trial, not a changed specular contribution or composition arithmetic.
D3D12 errors: 0. NRD Python regression tests: 154/154 passed.

This is attribution of a setting-induced difference, NOT measurement of each
lobe's absolute bias. Existing high-sample references contain total scene color,
not independently converged per-lobe ground truth. The reflection-proxy mask is
also not a specular-only image. Neither remaining error nor the complete previous
bias can be assigned to diffuse from this experiment alone. No NRD limitation or
integration bug is declared proven by this result.

Next diagnostic: increase per-frame input sample quality in controlled steps,
keeping camera, transport, guides and denoiser settings explicit. Determine
whether the residual error follows input variance, then use paired low-noise
signal/guide controls or converged per-lobe references where necessary. Keep the
application acceptance gates separate from NRD's algorithmic expectations; the
current 5%/Native-RMSE goals are not SDK guarantees. Defaults remain unchanged.

## Low-noise experiment admission check (2026-10-18)

Increasing the existing `samplesPerFrame` setting is NOT currently an NRD
low-noise experiment. `QueryNrdRelaxSamplingReadiness` explicitly rejects values
other than 1. Conversion assigns host gate 8, invalidates NRD inputs, and the
presentation selects Native fallback. This conservative contract is retained.

`shaders_PathTracing.hlsl` averages per-frame radiance over all sample offsets,
but stores primary surface, lobe distances/directions and continuation data from
sample offset 0. Removing the readiness guard without validating this pairing
would change the integration contract. It is not justified as a quality probe.
This finding is a limitation of the CURRENT APPLICATION PATH, not proof that
the NRD SDK cannot process a properly constructed multi-sample signal.

Runner: `Tests/PathTracing/probe_nrd_multispp_gate.py`.
Local report: `bin/PathTracingValidation/nrd-multispp-gate-20261018/report.json`.
Result: `passed-multispp-fallback-probe`, quality `not-evaluated`.
Six captures cover Native, NRD scene color and NRD validity at 4/16 spp after
30 elapsed frames (120/480 accumulated Native samples). Both cases reported the
incompatible-sampling fallback, had zero valid NRD input pixels, and produced
bit-identical Native fallback. D3D12 errors: 0. These captures must never be
counted as evidence that NRD improves or fails with low-noise inputs.

### Next controlled test sequence

1. Add a separate GPU diagnostic using the pinned SDK and existing D3D12 backend,
   without weakening application sampling readiness or changing Native transport.
2. Supply synthetic, paired signal/guide cohorts with known physical meaning:
   static plane, consistent view/projection/depth/normal, zero motion, positive
   in-lobe distance and explicitly valid resource states/lifetimes. Keep material
   restoration separate from the raw demodulated signal test.
3. Start with zero and spatially constant diffuse/specular radiance, including
   diffuse-only and specular-only controls. Use representable values, record
   packing error, reset state and actual applied SDK settings, and inspect
   checkpoints 1/30/120. These provide component ground truth without a noisy PT
   reference. Do not assume arbitrary edge or glossy signals must be exact.
4. Once those controls are characterized, add declared zero-mean noise and a
   fixed spatial edge. Separate interior bias from expected edge smoothing.
   Report per-component mean, maximum error, temporal behavior and all failures;
   define test bounds before running rather than tuning them to observed output.
5. Only then connect a low-noise PT signal or converged per-lobe reference with
   validated guides. Distinguish radiance-noise reduction from changes to hit
   distance sampling, history length or sample budget.

A synthetic control failure would motivate resource/encoding/SDK isolation;
a pass only rules out that tested failure mode, not every integration bug.
Persistent smoothing of a valid detailed signal is evidence about filter
behavior, not a universal SDK quality ceiling. Defaults remain 30/50 and off.
