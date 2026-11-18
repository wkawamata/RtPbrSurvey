# RTX 3090 static geometry heap comparison

2026-11-17. Only static vertex/index placement changes. Other buffers, shaders and draw ordering are unchanged. DEFAULT buffers are copied from staging resources, transitioned COPY_DEST -> GENERIC_READ before IA/shader/DXR reads. Staging stays alive through the existing scene-upload fence. -UseUploadGeometry selects the old reference path.

## Measurement

Same Release binary, RTX 3090 / driver 32.0.16.2124, 1920x1080, static 121 Helmets, minimal raster five-pass mode, VSync/debug/UI off. 600 warmup + 600 measured frames per process. Three runs per heap, ordered upload/default, default/upload, upload/default. Scene settings and adapter match in all six reports. Clocks are not locked. Values are medians of per-run averages (ms).

| Metric | UPLOAD | DEFAULT | Reduction |
|---|---:|---:|---:|
| GPU Total | 5.437387 | 0.847061 | 84.42% |
| GPU[2] Depth PrePass | 0.156489 | 0.118197 | 24.47% |
| GPU[3] GBufferPass | 5.128726 | 0.576217 | 88.76% |
| GPU[4] LightPass | 0.099536 | 0.115480 | -16.02% |
| GPU[5] ToneMapPass | 0.022461 | 0.024066 | -7.14% |
| CPU Frame | 5.327390 | 0.654485 | 87.71% |
| Wall Frame | 5.637674 | 0.991814 | 82.41% |
| CPU Record / submit | 0.329698 | 0.264060 | 19.91% |
| CPU GPU fence wait | 4.760125 | 0.137718 | 97.11% |

Per-run GPU Total averages:
- upload: 5.487010, 5.437387, 5.266541 ms
- default: 0.847061, 0.849299, 0.846935 ms

GPU Total improves by 6.42x and GBuffer by 8.90x in this case. Retain DEFAULT placement; do not generalize the speedup to other scenes/GPUs or compare it directly with old RTX 2080 measurements.

## Validation

- Release/Debug x64 builds pass.
- Debug viewer UPLOAD/DEFAULT: exit 0, D3D12 ERROR 0 each.
- Frame-60 PNGs are byte-identical. SHA256: 2C75A571F2F8DFFD0AB6409717E97B09C60F0A495DDF6E5827A0B00D73C0726A. DEFAULT image visually inspected.
- RendererCpuTiming and EvaluationState tests pass.
- All six reports passed five-pass, sample-count and metric-range checks.

Raw results: bin/Benchmark/geometry-heap-ab. Debug captures/logs: bin/Benchmark/geometry-heap-validation. Generated artifacts remain ignored.

- Additional Debug raster run: DEFAULT / 121 instances / 4 warmup + 8 measured frames, exit 0 and D3D12 ERROR 0.
