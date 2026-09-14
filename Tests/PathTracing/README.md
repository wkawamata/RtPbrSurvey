# Path Tracing Reference Capture

`Invoke-ReferenceCapture.ps1` runs two fixed-sample Path Tracing captures and writes JSON and Markdown reports.
It fails when the PNG hashes differ, a process fails, a capture times out, or a D3D12 error/corruption message is logged.

From the repository root:

```powershell
.\Tests\PathTracing\Invoke-ReferenceCapture.ps1 `
  -SceneName DamagedHelmet `
  -Samples 64 `
  -Seed 1
```

To restore a saved Evaluation Case, including its scene, camera, rendering settings, ROI, Japanese comments, and test
items:

```powershell
.\Tests\PathTracing\Invoke-ReferenceCapture.ps1 `
  -EvaluationCaseName "PT Normal Map" `
  -Samples 64 `
  -Seed 1
```

Generated captures, logs, and reports are written under `bin/x64/Debug/PathTracingReference` by default and must not
be committed.
