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
CLI/runtime wiring, capture metadata, a separate experiment executable, the
Native compatibility bridge, and the actual sensitivity captures remain pending.
