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

Commit 8 exposes these current-frame primary-surface resources through RenderGraph and Debug Texture Preview:

- `PathTracing.NormalRoughness`
- `PathTracing.ViewZ`
- `PathTracing.MotionVectors`
- `PathTracing.Albedo`

Use `-DebugPreviewResource <name>` with the normal Path Tracing CLI arguments to open one for visual validation.
Motion-vector previews use a centered 32x display scale by default, so zero motion remains neutral gray while small
positive and negative values remain visible. Run the stationary/camera-motion comparison with:

```powershell
.\Tests\PathTracing\Invoke-MotionVectorValidation.ps1
```

The generated captures and report are written under `bin/x64/Debug/PathTracingMotionVectorValidation` by default and
must not be committed.
