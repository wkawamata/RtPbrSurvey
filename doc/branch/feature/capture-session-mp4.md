# MP4 capture sessions

The shared capture session supports `Mp4` in both the reusable ImGui panel and
CLI (`-CaptureSessionFormat mp4`). Tank and the standalone RtPbrSurvey app use the
same implementation. `-CaptureSessionMp4BitrateMbps` and the MP4-only bitrate UI
select 1-100 Mbps (default 12). Frame rate is 1-240 FPS.

`Renderer/Mp4Encoder` writes one H.264 Main-profile MP4 without audio through the
Windows Media Foundation Sink Writer. The existing GPU readback is converted to
top-down SDR RGBA and then BT.709 limited-range NV12. Odd ROI sizes repeat the last
column/row to reach even encoded dimensions; no selected content is cropped.
Resolution, FPS, output path, and bitrate are fixed for each session. The writer
is incremental and belongs to the same thread as capture result processing.

Frame boundaries use `frameIndex * 10000000 / fps`, without accumulated rounding.
Low latency and zero B pictures keep playback timestamps aligned to the submitted
frames. Fixed-step uses the requested constant frame rate and the existing host
backpressure contract. Real-time passes presentation timestamps through the
readback request. The encoder retains one pending sample to determine its display
duration from the following timestamp; finalization extends the last frame to
the recording end time. Dropped frames leave temporal gaps where the preceding
image remains displayed, preserving playback speed and duration.

MP4 shares GIF's unused-filename selection (`name.mp4`, `name_000001.mp4`, ...).
The byte stream also opens with fail-if-existing to protect against a race with
another writer. An aborted or failed encoder removes its own incomplete file.
Finalization runs after draining and on Stop with no frame in flight, including
the existing GIF path. Finalization errors become session failures.

CMake and the standalone Visual Studio project link `mfplat`, `mfreadwrite`, and
`mfuuid`. There are no additional redistributables or external encoder processes.

Validation: `RtPbrSurvey.Mp4EncoderTests` encodes and decodes actual H.264, checks
frame count, duration/cadence, channels/orientation, odd dimensions, Unicode paths,
overwrite refusal, failure cleanup, CLI/UI configuration, and suffix selection.
Tank's `tests/Mp4CaptureSmoke.ps1` validates real GPU readback and MP4 output.

API references: [Sink Writer tutorial](https://learn.microsoft.com/en-us/windows/win32/medfound/tutorial--using-the-sink-writer-to-encode-video),
[low latency](https://learn.microsoft.com/en-us/windows/win32/medfound/codecapi-avlowlatencymode).
