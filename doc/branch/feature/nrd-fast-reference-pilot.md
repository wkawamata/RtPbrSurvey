# Reference Acquisition Speed Pilot

## Scope and Protocol

The remaining Diffuse mirror-proxy reference pair has 5.0129% NRMSE at
32768 samples, above the unchanged 3% qualification limit. Before a larger
budget, test whether process-local D3D12 Debug Layer opt-out materially
reduces acquisition cost without changing rendered radiance.

`-DisableD3D12DebugLayer` is explicit and process-local. Defaults remain
unchanged. It neither enables PT nor changes sampling, VSync or NRD settings.
Capture diagnostics report the requested state and whether this application
called `EnableDebugLayer`. They do not certify external tool configuration.

Runner: `Tests/PathTracing/pilot_nrd_fast_reference.py`.

- Keep the frozen reference builds and captures untouched.
- Build a separate Debug executable in `nrd-reference-fast-build`.
- Deploy all 72 reviewed CSO files from `nrd-reference-build/shaders` to the
  candidate executable directory. The application loads shaders next to its
  executable. Record source/runtime paths and hashes, and check them again
  after acquisition. Fresh compilation produced a different PT CSO hash;
  use the reviewed bytecode explicitly rather than assuming equivalence.
- Revalidate the historical reference chain and paired component report.
- Acquire Total, Diffuse and Specular at 256 samples with Debug Layer both
  enabled and disabled. Require bit identity to each reviewed replay.
- Acquire Diffuse at 2048 samples in ON/OFF/OFF/ON order. Require all four
  results to be bit-identical. Compare median elapsed time including startup
  and capture, with a predeclared useful-speedup threshold of 1.5x.
- Preserve seed 11, one sample/frame, pixel-center sampling, four bounces,
  environment mode 7, fixed camera and unchanged material/ROI settings.
  Direct lighting, emission, Russian roulette, NRD and distance diagnostics
  remain disabled for this fixture.
- Require successful capture completion, expected runtime/metadata, finite
  nonnegative radiance, alpha one and no error/corruption messages.
- Disabled runs are explicitly `not-checked-disabled` for D3D12 validation.
  An empty error log is not a D3D12 validation pass when validation is off.
- Stop on failure. A timeout terminates only the runner's own child process.
- Do not acquire higher-sample references or change acceptance limits as part
  of this pilot. The rejected 16-spp path remains excluded.

## Results

Report: `bin/PathTracingValidation/nrd-fast-reference-pilot-20261021/report.json`

SHA-256: `01b13ab260a4704e4341197e25a3ff335b6b3dd63e32d358d464c1ec8582fa95`.
Status: `completed-no-material-speedup`.

All six 256-sample captures are bit-identical to their reviewed component
replays. All four 2048-sample captures are bit-identical to the first normal
Debug control. Metadata, runtime state, fixture and artifact hash checks pass.
Five enabled runs have no D3D12 errors observed. The five disabled runs have
no error messages observed, but are not D3D12 validation passes.

| Order | Debug Layer requested | Samples | Elapsed seconds |
| --- | --- | ---: | ---: |
| 0 | ON | 2048 | 91.56 |
| 1 | OFF | 2048 | 100.57 |
| 2 | OFF | 2048 | 97.15 |
| 3 | ON | 2048 | 114.77 |

Median ON: 103.1649 seconds; OFF: 98.8582 seconds. The ratio is 1.04356x,
below the predeclared 1.5x target. Timing varies appreciably even within one
mode. This single ABBA pilot does not establish a general Debug Layer cost,
thermal/power explanation or statistically significant 4.4% improvement.
Do not adopt disabled validation as the high-sample acquisition strategy.

Candidate executable SHA-256:
`1b18afddfc00766c426905f41aef986a3e24c1157051b3e4e5277232ab62ebe1`.
Runtime PT shader SHA-256:
`bfab1bfca68586e917612fa30fa9ba70ac656761774a2ff7f692ba34ee935c56`.
This is a source-workspace diagnostic build, not a clean release artifact;
pre-existing Scene Editor edits remain outside this work unit. The acquisition
uses copied, validated fixtures rather than the locally edited scene asset.

Validation: Debug x64 build succeeded (23 existing LNK4199 warnings, zero
errors); three targeted C++ tests and 194 NRD Python tests pass. Four new
Python tests cover strict bit identity, invalid images, the fixed speed target
and missing/duplicate/invalid timing evidence.

## Next Work

No higher-sample captures were acquired in this pilot. Diffuse mirror-proxy
reference uncertainty is still unresolved; this experiment is not an NRD
quality pass or permission to change default pre-pass settings.

The next study should predeclare a larger fixed Diffuse reference budget and
two seeds, preserve the masks/camera/estimator and 3% qualification criterion,
and keep D3D12 validation enabled. If that pair still fails qualification,
retain an inconclusive verdict. After qualification, re-evaluate Diffuse and
joint contributions before changing NRD filtering or claiming an SDK limit.
