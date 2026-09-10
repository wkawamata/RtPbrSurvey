# Hybrid Reflection Production Quality Gates Worklog

## 2026-08-30

- Started `features/hybrid-reflection-production-quality-gates` from merge commit `ac5f6f0` on current `main`.
- Audited current defaults, controlled scenes, prior quantitative reports, runtime audits, and scoped subjective results.
- Defined separate production-baseline, production-candidate, diagnostic-experiment, and rejected-experiment classes.
- Fixed four evaluation profiles so variance-guided temporal is not silently combined with the spatial-policy comparison.
- Recorded the minimum scene coverage and acceptance gates. Current stochastic, temporal, fixed-spatial, bounded-spatial, and variance-guided features remain diagnostic/default-off; this phase has not promoted them.
- Added five versioned named ROI profiles covering controlled metallic roughness `0.0`, `0.35`, and `1.0` plus the two established DamagedHelmet noise regions. This removes manual coordinate copying from production-gate runs.
- Added `Invoke-ProductionQualityGates.ps1`. It validates paired sample/temporal sequences, unchanged resolved-radiance control variance, and a predeclared `0.5%` mean-preservation bound while leaving variance/tail changes as observations.
- An ad hoc roughness probe used an incorrect coordinate and produced a materially different result; it is explicitly excluded from gate evidence. The named `estimator-metal-r035` 64-frame smoke reproduced the established result: matching sequences, `+3.92%` temporal variance, `-16.97%` frame-difference p95, `-10.03%` p99, and `0.041%` mean difference. All invariants passed.
- Fixed the runner's no-`ProfileId` path after strict-mode testing exposed a null-array selection bug. The explicit single-profile path was unaffected.
- Ran all five named profiles at the 64-frame development level. Every profile matched sample/temporal sequences, preserved the resolved control, and stayed within the predeclared `0.5%` mean bound.
- Controlled roughness `0.0`, `0.35`, and `1.0` reproduced their established observations. The DamagedHelmet rearward surface was unchanged. The underside pipes changed mean by `0.448%`, increased temporal variance by `18.57%`, and reduced frame-difference mean/p99 by `15.70%`/`16.24%`.
- The invariant PASS is not a production-quality PASS. Mixed variance/tail behavior, an inactive rearward profile, and the prior no-visible-difference Lit result keep the bounded policy default-off and diagnostic.
- Completed the 256-frame standard gate for all five named profiles. Every profile again passed sequence, resolved-control, and mean invariants.
- Controlled roughness `0.0` reproduced mirror bypass (`-85.91%` variance relative to the fixed filter). Roughness `0.35` and `1.0` increased variance by `6.74%` and `10.70%` while reducing frame-difference p95 by `16.61%` and `24.05%`.
- The DamagedHelmet rearward surface remained bit-equivalent at report precision. The underside pipes changed mean by `0.426%`, increased variance by `21.44%`, and reduced frame-difference mean/p99 by `15.78%`/`15.99%`.
- The 256-frame standard evidence confirms bounded mean preservation but rejects a general minimum-variance or perceptible-quality-improvement claim. No 1024-frame extension is indicated for promotion because the policy already remains diagnostic on standard evidence.
- Audited the available additional glTF assets. BoomBox was selected because it contains both metallic-roughness and emissive textures and is more compact for fixed framing than Sponza.
- Added generic `-AutoSelectGltfAsset <name>` and `-UseSceneDefaults` automation. The existing dedicated DamagedHelmet and Estimator Test flags remain mutually exclusive with the generic selector.
- Extended `-ReflectionCameraDistanceScale` so Arcball framing can be applied to an auto-selected live scene without requiring screenshot automation.
- The BoomBox default distance was too wide for quality review. Versioned defaults with distance scale `0.25` produced a full-object 1920x1080 framing suitable for review. The 64-frame capture exited successfully with zero D3D12 errors and the two known buffer initial-state warnings.
- Completed the live Lit A/B review on BoomBox. Enabling the bounded spatiotemporal spatial policy over the fixed-spatial profile produced no perceptible visual difference. This is scoped non-regression evidence, not evidence of improvement.
- Because BoomBox also failed to establish a perceptible benefit and the 256-frame measurements show mixed behavior, the bounded policy is not promoted to a production candidate and remains a default-off diagnostic.
- The final Debug x64 build succeeded with zero errors and the known duplicate-vcpkg-task import warning. The BoomBox HDR smoke completed with exit code zero, zero D3D12 errors, and the two known buffer initial-state warnings.
- Performance remains unmeasured and is classified as `NOT CLAIMED`. This blocks production promotion but does not block closing the phase with the policy retained as a default-off diagnostic.

Status: done

