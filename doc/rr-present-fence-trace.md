# PF trace implementation and reproduction

PF-01/02/03 implementation, 2026-11-19. See rr-present-fence-investigation-plan.md
for the investigation and subsequent results for the actual findings.

The opt-in -RrFenceTrace <json> records QPC CPU events, submission/production/epoch
identity, original fence values, raw GPU timestamps and queue clock calibrations.
-RrFenceTraceWarmup and -RrFenceTraceSeconds select 1..120 seconds each. It closes
after the fixed-time observation, drains existing work normally and writes data.
-RrFenceTraceTimingOnly retains the same observation clock/counters without event
capture, clock calibration or extra GPU boundary queries, for overhead control.
-RrFenceTraceCalibrateVerifiedGpu adds periodic CPU/GPU clock calibration and
existing production-packet timestamp endpoints to a timing-only VerifiedPresent trace.
-RrFenceTraceCameraMotion translates the camera X by 0.4*sin(elapsed seconds).

No runtime decisions use trace data; Present, Signal, wait and publication ordering
are unchanged. CPU events use a bounded preallocated buffer (500000 events), GPU
points 200000 and clock samples 256. Overflow is explicit and invalidates a cohort.
JSON serialization/file output occurs only after the observation. Normal rendering
with no trace path does not sample QPC through these hooks or emit extra GPU queries.

Frame CPU IDs, host ticks, GPU submissions and RR production IDs are separate.
Serial input/evaluate submissions join through production+epoch. Present records
the RR output selected by the recorded graph. Raw GPU ticks use GetTimestampFrequency
and GetClockCalibration. Calibration call brackets and adjacent anchor residuals
are reported; CPU first-complete observation is not GPU fence arrival time.

GPU SDK begin/end and output validation/readback-copy end are extra timestamp
markers only during capture. Existing outer-pass durations change their segment
meaning under those extra markers, so PF analysis uses the explicit markers rather
than the ordinary RR pass row. No extra queue fence is introduced.

The parser rejects missing interior GPU packets, CPU/GPU timestamp regression,
submission/fence mismatch, changed scheduling order, window/count mismatch, failed
clock calibration and nonzero Present statuses such as occlusion. Absent pass
execution is not averaged as zero. Derived metrics retain p50/p95 and signed values:
a negative register-minus-GPU-end time means registration happened before GPU end.

## PF-01 baseline provenance

Snapshots of APPDATA/RtPbrSurvey/scene_config.json and evaluation_states.json were
saved locally under build/pf-investigation-20261119. Original files are never used
as child output: every process gets isolated APPDATA with copied configuration.

Saved Glass 01 refers to WarmGlassTable, 22 instances, 1 spp/16 bounces/seed 7,
RR/transmission enabled, but Russian Roulette ON. The persisted scene configuration
has Russian Roulette OFF and a different camera; native evaluate was OFF and its
four transmission experiment fields were absent. These are materially different
baselines, not interchangeable measurements.

Main reproduction uses the persisted Warm Glass Table configuration, with known
user-reported enabled RR/native/transmission toggles applied in an isolated case
named PF persisted Warm Glass Table. It keeps the persisted camera/lighting and
Russian Roulette OFF. Source hashes, overrides and ambiguity are in the local
baseline-manifest.json. The live session selection/camera was not captured, so this
is a persisted-setting reconstruction rather than an exact live-session replay.

The preliminary Glass 01 Debug smoke remains separate from main results. A second
persisted-setting Debug smoke passed with visible window and normal Present status.
No captures/configuration are uploaded; the JSON files remain local.

## Commands

```powershell
python -B Tests/PathTracing/test_rr_fence_trace.py
python -B Tests/PathTracing/run_rr_fence_investigation.py --exe build/reconstruction-input-validation/Release/RtPbrSurvey.exe --baseline build/pf-user-baseline-20261119 --case "PF persisted Warm Glass Table" --output build/pf-static-new --cohort static --pairs 2 --warmup 5 --seconds 30
```

The static cohort uses ABBA then BAAB VSync comparisons, first blocking and then
nonblocking (16 processes). Motion uses the same wall-time path with --cohort motion
and --pairs 1 (four processes). --cohort overhead compares trace vs timing-only in
ABBA at VSync OFF / blocking (four processes). Use new/empty output folders.

The app window is shown for presentation measurement. The harness guards against
other RtPbrSurvey processes, records nvidia-smi clock/power/utilization samples and
pins executable/runtime/shader/asset/config/script hashes. It never terminates other
workspace apps. Build/benchmark workloads must not overlap. Child timeouts stop
only that harness's own child. Raw logs and data live under ignored build folders.

## 同じプロセス内でのVSync比較

詳細トレースに -RrFenceTraceVSyncCycle を付けると、計測区間を三等分し、
VSyncをON→OFF→ONと切り替える。起動時はVSync ON、詳細トレース12秒以上が必要。
集計の presentSyncSegments は起動時の設定値ではなく、各Presentの実際の
SyncIntervalで区間を識別する。切替の前後1秒は比較から外す。各区間で
Present頻度、新規RR画像の採用頻度、CPU側のRR評価APIの時間を比較できる。

## 同じプロセス内でのフレーム資源取得の比較

詳細トレースに -RrFenceTraceNonblockingCycle を付けると、VSync ONの
状態でBlocking→Nonblocking→Blockingを比較できる。12秒以上の詳細
トレースを指定し、VSync切替オプションとは併用しない。
保留中の資源取得がある場合は、取得を済ませてから次のモードへ切り替える。

presentAcquisitionSegments は各Present後の実際の資源取得モードで区間を
識別し、切替の前後1秒を除く。resourceBlockingWaitMsはCPUがその場で
待ったフレームの時間、resourceDeferralMsは待たずに戻ってから再取得する
までの時間を示す。各区間のmeasuredPresentsに対する件数と、
freshPresentFpsを合わせて確認する。
