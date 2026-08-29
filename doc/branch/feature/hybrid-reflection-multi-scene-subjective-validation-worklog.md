# Hybrid Reflection Multi-Scene Subjective Validation Worklog

## 2026-08-29: Controlled Estimator live Lit evaluation

Branch: `features/hybrid-reflection-multi-scene-subjective-validation`

Base: `c881cb4` (`Add Hybrid Reflection multi-scene quality gates (#37)`)

The user evaluated the live `Hybrid Reflection Estimator Test` scene in Lit view. Both variants used Hybrid Reflection contribution, stochastic rough sampling, and history weight `0.9`. A disabled variance-guided temporal/persistent confidence; B enabled it.

User observations:

- stationary granular noise: B may be slightly lower, but the difference is weak and not conclusive;
- motion lag/ghosting: did not increase in B;
- direction reversal: no breakdown;
- post-motion stop: converged stably;
- brightness/detail: no unnatural loss was observed.

Decision: B showed no dynamic or composition regression in this controlled live test. The stationary result is recorded only as a weak improvement tendency; it does not establish a clear perceptual benefit or production readiness.

Next: evaluate DamagedHelmet live, treating the underside pipes and the known rearward noise region as separate observations.

## 2026-08-29: glTF asset-path blocker

Loading DamagedHelmet through the UI in an app launched by Computer Use triggered `assert(loaded)` in `GltfObjectViewerScene::Load()`. The scene descriptor's relative `Assets\\...` path was resolved against the process current directory, so a launch outside the repository could not find the asset.

A glTF-loader-local path resolver now preserves absolute paths and valid current-directory paths, then resolves otherwise-missing relative paths against the executable directory. This preserves normal repository-root launches while making desktop-launcher, Computer Use, and alternate-working-directory launches deterministic.

Validation:

- Debug x64 build: passed with zero errors and one known MSBuild import warning.
- launch with working directory fixed to `C:\\Windows` and `-AutoSelectGltfDamagedHelmet`: exit code 0.
- PNG capture after 20 warm-up frames: succeeded.
- D3D12 Debug Layer: zero errors and two known committed-buffer initial-state warnings.

This establishes that DamagedHelmet loads outside the repository working directory. Subjective A/B evaluation remains incomplete and will resume when the user returns.

## 2026-08-29: DamagedHelmet live Lit evaluation

After the asset-path fix, the user reloaded DamagedHelmet and evaluated it from a manually adjusted and saved camera. The framing simultaneously exposed the rearward surface adjacent to the smooth crown panel, the bright underside pipe, and the black curved hoses.

Both variants used Lit view, Hybrid Reflection contribution, stochastic rough sampling, history weight `0.9`, surface variance filter disabled, and rejected-pixel neighborhood disabled. A disabled variance-guided temporal; B enabled it.

User observations:

- stationary noise: no perceptible noise in either A or B;
- motion lag/ghosting: absent in both;
- direction reversal: no reflection breakdown;
- post-motion stop: stable in both;
- brightness/detail: no perceptible change.

Decision: A and B were perceptually equivalent under this DamagedHelmet live Lit condition. No dynamic or composition regression was observed in B, but no stationary-noise benefit was established either. This result is not subjective evidence of variance reduction, production readiness, or general image-quality improvement.

## Overall decision

- Controlled Estimator: weak stationary-noise improvement tendency, with no dynamic regression or brightness/detail loss.
- DamagedHelmet: A/B equivalent; neither benefit nor regression was perceptible.
- Keep the policy default-off.
- The result is consistent with the #38 objective multi-scene gate, while showing that measured numerical improvement is not necessarily perceptible in Lit composition.
