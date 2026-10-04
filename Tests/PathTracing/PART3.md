# Scene scale and self-intersection measurement

Editable fixtures are `scale-small`, `scale-medium`, and `scale-large` under
`Assets/Scenes/PathTracingValidation`. Each has a saved camera and relative render preset.
Scale is 0.01, 1, or 100. Camera position/target/clipping planes, geometry, light
position and range scale together. Point intensity is multiplied by scale squared,
preserving inverse-square illumination and the range window. No shader or engine changes.

Run from repository root with Python and PowerShell 7:

```powershell
python -B Tests/PathTracing/test_validate_scale.py
python -B Tests/PathTracing/validate_scale.py --smoke --output bin/PathTracingValidation/part3-smoke
python -B Tests/PathTracing/validate_scale.py --output bin/PathTracingValidation/part3-scale
python -B Tests/PathTracing/complete_scale_suite.py --output bin/PathTracingValidation/part3-extra --powershell pwsh
python -B Tests/PathTracing/summarize_scale.py --main bin/PathTracingValidation/part3-scale --extra bin/PathTracingValidation/part3-extra
```

Use fresh output directories. Each process starts accumulation afresh at seed 7,
4 spp, one bounce, radiance output, no environment/emission, direct lighting only,
RR off. Debug x64 captures are 1920x1080 linear RGB PFM. Logs reject D3D12 errors,
and captures reject nonfinite values or incorrect dimensions/sample settings.
Failures remain in the output report. This is a deterministic visibility experiment,
not a convergence or performance comparison.
The summary script needs matplotlib and numpy; `--packages` accepts an existing
local package directory. Native executable builds are reused because this part
changes only scene data, scripts and documentation.

Open an editable fixture with the application:

```powershell
.\bin\x64\Debug\RtPbrSurvey.exe -SceneFile Assets\Scenes\PathTracingValidation\scale-small\scene.json -EnablePathTracing
```

Replace `scale-small` with `scale-medium` or `scale-large` as needed. Source fixtures
use relative settings. The generated fixed-policy variants in the measurement
directory provide the minimal light-leak reproduction.

The main matrix has 57 captures:

- 36 visibility captures: 3 scales, front/angled Point light, relative/fixed
  settings, clear/between/beyond variants. The thin beyond slab is physically
  beyond the light, identically placed for both policies.
- 12 contact captures: relative, fixed, fixed bias alone, fixed TMin alone.
  Compare each column with a matching clear-floor image; baseline-shadow columns
  have ratio below 0.5 and leaked columns ratio above 0.9.
- 9 convex sphere captures: shadow disabled reference, relative settings, and
  both offsets zero. Compare positive reference channels for unexpected darkening.

Relative normal bias is `0.01*s`, TMin `0.001*s`, TMax `10000*s`.
Fixed policy holds bias 0.01 and TMin 0.001. Two mixed policies isolate these factors.
TMax remains proportional in this matrix to avoid conflating primary clipping
with shadow artifacts. Supplementary captures use TMax `2*s` and `20*s` separately;
this setting also limits primary rays. It is not solely a shadow-distance setting.
The existing near-light script is reused for Point/Spot and front/angled layouts.

ROIs, in top-left pixel coordinates:

| Measurement | x,y,width,height |
| --- | --- |
| Visibility | 944,524,32,32 |
| Contact floor | 1060,524,128,32 |
| Convex sphere | 936,516,48,48 |

An independent pinhole/AABB calculation checks that the contact ROI misses the
primary cube silhouette. Rendered contact profiles measure shadow extent and
light leaks; they do not measure a unique world-space physical gap.
Visibility expects clear mean >0.001, blocked ratio <0.8, beyond max difference
<=1e-6. Numerical scale agreement is measured separately because small shader
distance clamps can violate ideal inverse-square similarity.

Raw reports, generated variants, PFM and figures stay ignored under `bin`.
Results and limitations are recorded in the Part 3 summary and final report.
