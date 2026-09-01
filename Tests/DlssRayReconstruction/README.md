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
