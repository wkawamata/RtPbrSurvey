# DLSS Ray Reconstruction RTX 4090 Validation Result

GPU: NVIDIA GeForce RTX 4090
Driver version: 616.56
OS: Windows 10 Enterprise 25H2 build 26200.9168
Branch: `codex/dlss-ray-reconstruction`
Initial tested commit: `19befab Clarify RTX 4090 validation result handoff`
Support-query retest: RR branch worktree after merging `origin/main` at `b20cd87` and adding RR runtime deployment

DLL check:

- `sl.interposer.dll`: present, Streamline 2.12.0.0
- `sl.common.dll`: present, Streamline 2.12.0.0
- `sl.dlss.dll`: present, Streamline 2.12.0.0
- `sl.dlss_d.dll`: present, Streamline 2.12.0.0; deployed automatically by MSBuild after the fix
- `nvngx_dlss.dll`: present, version 310.7.0.0
- `nvngx_dlssd.dll`: present, version 310.7.0.0; deployed automatically by MSBuild after the fix

Build:

- Debug x64 MSBuild: succeeded with 0 errors
- One known duplicate vcpkg import warning was emitted
- Visual Studio 2022 Professional MSBuild was used because the documented Community path is not installed on this workstation

Copy fallback command:

```powershell
.\bin\x64\Debug\RtPbrSurvey.exe -AutoSelectGltfDamagedHelmet -EnableDlssRayReconstruction -CaptureReflectionResolvedRadiance -CapturePath Screenshots\rr_copy_fallback_4090.png -CaptureAfterFrames 60 -ExitAfterCapture -LogToFile rr_copy_fallback_4090.log
```

Copy fallback result:

- exit code: 0
- capture path: `Screenshots\rr_copy_fallback_4090.png`
- capture generated: yes, 1920x1080
- support: unavailable
- status: Unsupported adapter
- supportQueryResult: `Result::eErrorFeatureNotSupported`
- inputReadiness: unavailable
- inputReason: Native evaluation disabled
- lastEvaluate: unavailable
- lastEvaluateStatus: SDK not integrated
- lastEvaluateResult: Unavailable
- lastEvaluateOutput: unavailable
- D3D12 ERROR: 0
- D3D12 WARNING: 2; both are the known buffer initial-state warning where `D3D12_RESOURCE_STATE_UNORDERED_ACCESS` is ignored in favor of `D3D12_RESOURCE_STATE_COMMON`
- device removed/crash: no
- visual/capture notes: non-black DamagedHelmet resolved-radiance image; 1024 samples, 139 non-black samples, mean luma 9.10; no full-frame noise or NaN-like corruption observed
- capture SHA-256: `22206C03C363108A3636371BCAE2A31F342B17E366092426B00314282D79D090`

Native RR command:

```powershell
.\bin\x64\Debug\RtPbrSurvey.exe -AutoSelectGltfDamagedHelmet -EnableDlssRayReconstruction -EnableExperimentalNativeRayReconstruction -CaptureReflectionResolvedRadiance -CapturePath Screenshots\rr_native_4090.png -CaptureAfterFrames 60 -ExitAfterCapture -LogToFile rr_native_4090.log
```

Native RR result:

- exit code: 0
- capture path: `Screenshots\rr_native_4090.png`
- capture generated: yes, 1920x1080
- support: unavailable
- status: Unsupported adapter
- supportQueryResult: `Result::eErrorFeatureNotSupported`
- inputReadiness: unavailable
- inputReason: Native evaluation disabled
- lastEvaluate: unavailable
- lastEvaluateStatus: SDK not integrated
- lastEvaluateResult: Unavailable
- lastEvaluateOutput: unavailable
- D3D12 ERROR: 0
- D3D12 WARNING: 2; both are the known buffer initial-state warning where `D3D12_RESOURCE_STATE_UNORDERED_ACCESS` is ignored in favor of `D3D12_RESOURCE_STATE_COMMON`
- device removed/crash: no
- visual/capture notes: non-black DamagedHelmet resolved-radiance image; 1024 samples, 139 non-black samples, mean luma 9.10; no full-frame noise or NaN-like corruption observed
- capture SHA-256: `22206C03C363108A3636371BCAE2A31F342B17E366092426B00314282D79D090`

Raw Streamline result for both runs:

```text
[RR] support=unavailable status=Unsupported adapter supportQueryResult=Result::eErrorFeatureNotSupported inputReadiness=unavailable inputReason=Native evaluation disabled lastEvaluate=unavailable lastEvaluateStatus=SDK not integrated lastEvaluateResult=Unavailable lastEvaluateOutput=unavailable
```

## Support-query retest after runtime deployment fix

The MSBuild and CMake runtime lists were updated to deploy both `sl.dlss_d.dll`
and `nvngx_dlssd.dll`. Debug x64 was rebuilt successfully with 0 errors, then
the RTX 4090 support query was rerun without opting into native evaluation.

Result:

- exit code: 0
- support: available
- status: Available
- supportQueryResult: `Result::eOk`
- inputReadiness: ready
- inputReason: Ready
- D3D12 ERROR: 0
- D3D12 WARNING: 2; both are the known buffer initial-state warning
- device removed/crash: no

```text
[RR] support=available status=Available supportQueryResult=Result::eOk inputReadiness=ready inputReason=Ready lastEvaluate=available lastEvaluateStatus=Invalid integration lastEvaluateResult=NativeEvaluationDisabled lastEvaluateOutput=fallback
```

Conclusion:

- The build, automated capture, fallback safety, and D3D12 stability checks passed on this RTX 4090 workstation.
- The initial Streamline support query failed because the RR plugin and NGX RR runtime were not both deployed beside the executable.
- After automatic deployment of `sl.dlss_d.dll` and `nvngx_dlssd.dll`, the RTX 4090 support query passes with `Result::eOk` and reports `support=available`.
- Native RR evaluation and image quality remain separate validation work; this retest deliberately left experimental native evaluation disabled.
- The copy-fallback and native-request captures are byte-identical. This is consistent with both runs following the same unsupported-feature fallback path.
- The RTX 4090 now satisfies the support-availability acceptance criterion. Native-output acceptance is not yet established.
