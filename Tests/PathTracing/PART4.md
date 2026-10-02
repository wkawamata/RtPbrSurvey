# PathTracingPass GPU performance measurement

`measure_performance.py` uses existing GPU checkpoints through `-LogFPS 1`.
The measured value is `[GPU Pass] PathTracingPass`, not CPU FPS or whole-frame time.
It is the latest completed GPU frame observed once per CPU frame; the logger has
no independent GPU frame ID. Identical numeric durations are not deduplicated.

Baseline: 1920x1080, one sample/frame, max two bounces, one Point light,
three metal spheres and a floor, sphere stacks/slices 24/32. Constant-environment
MIS mode 5, unit environment, direct lighting on, RR/emission off, seed 7,
static saved camera and scene, accumulation on. The editable baseline is
`Assets/Scenes/PathTracingValidation/performance/scene.json` with a relative preset.

Vary one factor at a time:

| Factor | Values |
| --- | --- |
| Render resolution | 960x540, 1280x720, 1920x1080 |
| Samples/frame | 1, 2, 4 |
| Bounce limit | 1, 2, 4 |
| Point light count | 1, 4, 8 |
| Sphere stacks/slices | 12/16, 24/32, 96/128 |

Lights are co-located with total intensity 8, preserving incident illumination.
The geometry axis changes tessellation, not instance count or radius. It changes
the polygonal sphere approximation slightly. Identical spheres share a mesh/BLAS:
unique mesh triangles including floor are 386/1538/24578; summed instance triangles
are 1154/4610/73730. Pole-degenerate triangles emitted by the generator are included.
This does not measure TLAS instance-count scaling or arbitrary large assets.

After the pilots, select 16 positive GPU timing observations for warm-up and retain
the next 32. Capture at CPU automation frame 64 leaves a 16-frame margin for readback latency.
Each case runs three times in separately seeded shuffled rounds (84,85,86),
33 launches total. Pool 96 observations/case for median, p90 and p95 (linear
interpolation, type 7); retain every run's statistics and raw 32-row CSV.
Per-run median variation and GPU clock changes limit the interpretation.

Use frame-based capture, not `-PathTracingSamples`: the latter deliberately forces
samples/frame to 1. The App logging extension also emits PT settings diagnostics
for frame-based captures. Timing ends before the capture-queued diagnostics line,
so screenshots/readback are excluded. GPU aggregate fields in that diagnostic
are not used: they are populated only by exact-sample capture automation.

The script changes only its own child window to borderless and matches the window's
DPI context, ensuring exact client dimensions. Final diagnostics must match
resolution, samples/frame, bounce limit and seed. Too few GPU timings, missing
GPU timing support, accumulation resets or D3D12 errors reject the run.
The benchmark window is visible and foreground activation is attempted and recorded.
Hidden, visible and foreground pilot runs all eventually exhibited roughly 4 fps
frame progression and P8 clocks. Thus visibility alone did not explain or resolve
the low-power state. The pilots are separate from the final fixed protocol.
The original 60/120-observation pilot protocol was reduced before the final cohort
because frame progression is slow while P8 clocks and positive GPU durations remain
stable. This is a short-window empirical distribution, not a thermal steady-state
or maximum-throughput benchmark. P95 has only about five upper-tail observations
per pooled case; do not interpret it as a reliable rare-stall estimate.

Power conditions: record AC/battery status, active Windows power scheme and GPU
name/driver/P-state/temperature/clocks/power before and after each run. During
each launch, poll GPU telemetry roughly twice per second, attaching the most
recent logged CPU frame. This is approximate correlation with completed GPU
timestamps, not a cycle-accurate clock trace. Missing telemetry remains explicit.
Power settings, clock controls, driver settings and VSync are not changed.
The renderer presents with sync interval 1. Debug x64 results include Debug
validation effects and cannot be treated as Release throughput.

```powershell
python -B Tests/PathTracing/test_measure_performance.py
python -B Tests/PathTracing/measure_performance.py --smoke --repeats 1 --output bin/PathTracingValidation/part4-resolution-smoke
python -B Tests/PathTracing/measure_performance.py --cases samplesPerFrame-1 --repeats 1 --output bin/PathTracingValidation/part4-frame-smoke
python -B Tests/PathTracing/measure_performance.py --output bin/PathTracingValidation/part4-performance-final
python -B Tests/PathTracing/summarize_performance.py --output bin/PathTracingValidation/part4-performance-final
```

Use fresh output directories. `--cases` selects named cases for isolated reproduction.
The summary needs matplotlib; `--packages` accepts an existing local package directory.
Failures and artifacts remain ignored under `bin`; result hashes and representative
statistics are committed in `doc/branch/feature/path-tracing-validation-results/part-4-summary.json`.

Startup/resize history clear and initial BLAS/TLAS/environment setup are discarded
by warm-up. The steady static scene does not request TLAS edits or camera changes.
Mode 5 samples a constant environment and does not request importance-distribution
construction. Resource barriers, bindings and dispatch belong to the checkpoint
interval; this is a pass measurement, not an isolated shader-instruction benchmark.
Animated scenes, accumulation clears, TLAS rebuilds and importance modes 4/7 need
separate measurements. Capture/readback may affect subsequent GPU clocks, so each
new process warms up again and execution order is shuffled.
