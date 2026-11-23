# RRQ-03 input safety preparation

2026-11-17: RRQ-03-A completed; RRQ-03-B numeric SDK probes passed.
Same-queue scheduling (03-C) is a separate change and is not included here.

- Added RR-only input envelope v1 and canonical placeholder, preserving original rejection status.
- Added status 7 for numeric envelope failures; fractional/unknown categories remain unknown.
- Existing conversion is unchanged by default. Process-only `-PathTracingRrSchedule safe-serial`
  enables v1; it does not itself enable RR/native evaluation or relax material admission.
- Background candidates use a unit guide normal only under the experimental profile.
- Conversion/root constants grow from 39 to 40 DWORDs in the live diagnostics shader.
- Shared PT and NRD inputs are unchanged. No additional image copy or full-screen dispatch.

Validation: ordinary VS Debug build passed (one existing MSB4011 warning).
WARP: 32 safety cases, 32 legacy conversion cases, 40 live conversion cases,
48 optimized/reference/CPU summary cases, and metadata admission tests passed.
Texture conversion at 1x1, 7x9 and 17x19 passed exact accepted-surface parity,
canonical rejected tuples, status preservation and full output coverage.
No D3D12 errors were reported by these GPU fixtures.

RTX 3090 SDK probes passed for all-placeholder and mixed envelope-boundary tuples:
one evaluate each, completed GPU work, finite output RGB and zero D3D12 errors.
These are synthetic numeric probes, not material/radiance correlation or quality tests.
The explicit numeric-safety-probe metadata is test-only; normal replay admission remains unchanged.
The upper limits are an experimental application envelope, not a vendor-wide guarantee.

Local evidence: build/rrq03-safe-textures-20261117/report.json,
build/rrq03-sdk-probes-20261117/report.json and per-probe native-result.json/logs.
Build logs live under build/agent-builds/rrq03-*; generated files are excluded from Git.
The initial GPU check found Work5 PID 32912 active, so no SDK probe was started then.
The probes ran only after a later check found no RtPbrSurvey process and 1 percent GPU load.
No external process was stopped.

Reproduction (Python with no bytecode cache):

```powershell
python -B Tests/PathTracing/validate_rr_safe_textures.py --runner build/reconstruction-input-validation/Debug/RtPbrSurvey.RrTextureConversionTests.exe --legacy-shader build/reconstruction-input-validation/Tests/shaders_PathTracingRrConversion_CSMain.cso --safe-shader build/reconstruction-input-validation/Tests/shaders_RrSafeTexture_CSMain.cso --output build/rrq03-safe-textures-new
python -B Tests/PathTracing/probe_rr_safe_sdk.py --run-sdk --replay build/reconstruction-input-validation/Debug/RtPbrSurvey.RrNativeReplay.exe --placeholder-shader build/reconstruction-input-validation/Tests/shaders_RrPlaceholderProbe_CSMain.cso --boundary-shader build/reconstruction-input-validation/Tests/shaders_RrSafeSdkProbe_CSMain.cso --output build/rrq03-sdk-probes-new
```

Both scripts require an empty output folder. SDK execution requires an isolated GPU;
its process guard detects RtPbrSurvey, while the operator must check other GPU workloads.
Without --run-sdk the second script prepares fixture data without using the GPU.
