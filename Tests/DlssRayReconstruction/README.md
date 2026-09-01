# DLSS Ray Reconstruction Deterministic A/B

Run from the repository root after a Debug x64 Streamline build:

```powershell
.\Tests\DlssRayReconstruction\Invoke-DeterministicAb.ps1
```

If capture processes are run separately, analyze existing `fallback.*` and
`native.*` artifacts with:

```powershell
.\Tests\DlssRayReconstruction\Invoke-DeterministicAb.ps1 -AnalyzeOnly
```

The default comparison samples every 16th pixel in each axis. Use
`-SampleStride 1` when a slower full-pixel report is required.

The script launches fresh fallback and native processes with the same
DamagedHelmet scene, default camera, resolved-radiance view, and warm-up count.
It requires fallback for variant A, successful native output for variant B,
and zero D3D12 errors in both runs.

The generated JSON and Markdown reports include diagnostics, capture hashes,
changed-pixel ratio, normalized RGB MAE/RMSE, maximum channel error, displayed
luminance, and non-black ratios. Artifacts are written under
`Screenshots/DlssRayReconstructionAb` by default and must not be committed.

This establishes deterministic execution and a visible output difference. It
does not establish physical correctness or production image quality.

## Temporal A/B

Run the motion, direction-reversal, and settling comparison with:

```powershell
.\Tests\DlssRayReconstruction\Invoke-TemporalAb.ps1
```

The temporal plan captures both variants while the camera moves forward,
reverses direction, and remains stopped for 1, 6, and 15 frames. The report
compares fallback against native RR at each frame and measures each variant's
remaining change relative to the final settling capture. Existing artifacts
can be reanalyzed with `-AnalyzeOnly`. Generated files are placed in
`Tests/DlssRayReconstruction/captures-temporal` and must not be committed.

## Input Contract Captures

Capture the RR noisy input color, normal, motion-vector, and depth debug views
with:

```powershell
.\Tests\DlssRayReconstruction\Invoke-InputContractCapture.ps1
```

The report checks capture dimensions, non-black coverage, unique hashes, RR
support diagnostics, and D3D12 errors. These displayed views detect missing or
obviously corrupt inputs; they do not prove raw-value sign, scale, or coordinate
space correctness. Existing artifacts can be reanalyzed with `-AnalyzeOnly`.
