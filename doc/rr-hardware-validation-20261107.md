# RR hardware verification, 2026-11-07

Source commit: 742926f. RTX 2080 Ti; RR was available in this run. The earlier
feature-unavailable condition is not claimed fixed by a code change.

## Validation shader equivalence

Hardware RrValidationTests passed all 48 fixtures against both the frozen scalar
shader and independently computed CPU summaries; captured D3D12 errors 0.
Input status masks, output finite RGB, ignored alpha, partial edge groups and exact
pixel coverage match. Evidence: performance/rr-validation-hardware-20261107.log.
All evidence paths below are relative to build/reconstruction-input-validation/.

Single-dispatch GPU diagnostics (debug layer ON, no controlled repetitions):

| Payload | Size | Group reduction (ms) | Scalar reference (ms) |
|---|---|---:|---:|
| Valid input | 720p | 0.041824 | 0.034816 |
| Mixed invalid input | 720p | 0.042240 | 0.694272 |
| Valid output | 1080p | 0.072032 | 0.067584 |
| Mixed nonfinite output | 1080p | 0.075040 | 0.387552 |

The reduction helps these invalid-heavy fixtures, but valid-input diagnostics are
slightly slower. The structural 64x reduction in global counter additions does NOT
imply a 64x speedup, nor demonstrate improvement on the common valid path.

## Release cohort

Normal VS Release; static rr-quality, 4 bounces, center primary, Accumulate OFF,
VSync OFF, app debug layer OFF. Eight conditions, three repeats each, warmup 60,
measurement 240 display observations. All 24 runs passed backend/resolution/history
and timing validation; no captured ERROR/CORRUPTION. No builds ran during this cohort.
Performance evidence: performance/rr-after-reduction-20261107/report.json and
performance/rr-after-reduction-summary-20261107.json.

| Size | spp | Fresh RR outputs/s (mean) | Range of 3 runs | Conversion ms | SDK pass ms |
|---|---:|---:|---|---:|---:|
| 1280x720 | 1 | 31.38 | 30.14..32.04 | 0.365 | 11.50 |
| 1280x720 | 4 | 22.79 | 21.98..23.68 | 0.375 | 11.31 |
| 1920x1080 | 1 | 18.13 | 17.74..18.46 | 0.842 | 24.65 |
| 1920x1080 | 4 | 11.76 | 9.83..12.75 | 0.813 | 25.17 |

The SDK pass still includes output poisoning/validation; it is not isolated NGX GPU
time. Compared with the prior session, 1080p rates are higher, but there is no
contemporaneous randomized old/new end-to-end A/B. Clock/power state and software
initialization were not controlled. Do not attribute that difference to reduction.
Static throughput is not an equal-quality Native/RR or animated-quality comparison.

## Packet timing

Successfully verified completions were counted once each, excluding warmup.
All values below are means of the per-run CPU steady-clock observation means.
They include queue/submission/polling effects and are not GPU stage durations.

| Size | spp | Input wait ms | Admitted wait ms | Evaluate CPU ms | Output wait ms | Packet total ms |
|---|---:|---:|---:|---:|---:|---:|
| 1280x720 | 1 | 6.60 | 2.24 | 4.37 | 12.91 | 26.14 |
| 1280x720 | 4 | 17.96 | 2.33 | 4.79 | 12.74 | 37.85 |
| 1920x1080 | 1 | 13.44 | 2.29 | 7.56 | 26.16 | 49.47 |
| 1920x1080 | 4 | 38.12 | 2.49 | 12.46 | 27.02 | 80.10 |

Input and output pending phases each saw two Poll calls per completed packet.
Input wait can include preceding PT GPU commands as well as admission validation;
CPU work before RecordInput is outside packet total. Evaluate CPU measures the
entire adapter API call, not isolated SDK internals. Packet total excludes the gap
from publication to the next RecordInput. The phase sum leaves setup/recording gaps.
These observations motivate separating SDK work from validation and investigating
fence-observation/scheduling latency; they do not justify removing safety checks.

## Runtime artifacts

- bin/x64/Release/RtPbrSurvey.exe: SHA256 f5eeaf94ab40c7e019fef9ee38c43b5a541fec798adfe66011e7dd295e32dedf
- bin/x64/Release/shaders_PathTracingRrValidation_CSMain.cso: SHA256 60654385ffc93e2c512149b275610ad0760c26d744f497c6ae45f1519de5ad11
- bin/x64/Debug/RtPbrSurvey.exe: SHA256 7ea185d1fe4d65af1c07bdbcf3be190dc5e0d1feee33bb7468371cc588c5b95a
- bin/x64/Debug/shaders_PathTracingRrValidation_CSMain.cso: SHA256 839e3290c90e1e1d285690cf10260cd7c79390e028e4d60719b5f69d56496f16

## Debug runtime regression

- Normal VS Debug: static live RR suite 10/10 passed (output/viewport, Native OFF,
  background, non-mirror, thin-transmission guard, multispp, jitter primary,
  disabled-distance-guide guard, debug-output guard). Evidence:
  user-test-rr/reduction-static-20261107/report.json.
- Normal VS Debug: full-scene rotation and translation at 640x360 each passed all
  five lifecycle checkpoints, verified finite output and stationary background
  motion exactly zero. Pause/resume and continuous history checks passed.
  These full-scene tests do not establish per-instance per-pixel motion ground truth.
  Evidence: user-test-rr/reduction-object-full-20261107/report.json.
- Refreshed NRD/RR CMake Debug: 19/19 camera/backend/sampling/projection/resize
  checkpoints passed at 4 spp, frame jitter, 4 bounces, Accumulate ON, general
  background. Includes unsupported-input fallback/recovery and in-flight cancellation.
  Camera translation/rotation reprojection and changing RR imagery passed; D3D12
  errors 0. Evidence: user-test-rr/reduction-camera-lifecycle-20261107/report.json.
  Production CMake CSOs were copied from shaders/ into its Debug runtime directory;
  standalone test CSOs were not substituted. This includes the updated validation.

- build/reconstruction-input-validation/Debug/RtPbrSurvey.exe: SHA256 002f4d6442e1b20f9e3ec212a60c2db18b04310a910a37f9fafb31827248d49e
- build/reconstruction-input-validation/Debug/shaders_PathTracingRrValidation_CSMain.cso: SHA256 42b447bd5a0aae06ee3f9397ea019038106cd753814d1e33d62e1527bcc12c29

Hardware verification of the current change is complete. There is no new subjective
quality claim: previously accepted moving-shadow ghosting remains a known limitation;
transmission/refraction RR remains separate TODO. PR merge and remote synchronization
have not been performed as part of this verification.
