# Realtime Camera Under Encoder Pressure

## Setup

- Commit base: `f42ce0cf8c45ccdd045e8969ed94eb56cb7534bf`; source and executable
  hashes for the tested working tree are recorded in the report.
- Output: `bin/CapturePort/realtime-camera-pressure-20261014-acceptance`
- SDK-present Debug x64 app, asymmetric cube, 640x480 center ROI
- 60 FPS requested, four seconds, 8 Mbps H.264; scripted right-drag camera
  movement throughout the recording
- Comparison: normal encoder and 100ms diagnostic delay before each append

## Results

- Both runs completed and decoded as 640x480 H.264 MP4. Every MP4 sample PTS
  matched the accepted frame's logged realtime timestamp; sample durations were
  positive, timestamps strictly increased, and media duration was four seconds.
- The session had 240 scheduled capture intervals. Baseline accepted/saved 54
  and counted 186 drops. With encoder delay it accepted/saved 31 and counted
  209 drops. In both runs, accepted + dropped equaled 240.
- Input-to-next-captured-pose latency was 76ms p95 / 87ms max at baseline and
  134ms p95 / 179ms max with encoder delay. The output-frame interval peaked at
  93ms and 185ms respectively.
- Maximum measured append time was 76.569ms at baseline and 173.192ms with the
  configured 100ms delay. No D3D12 error or capture failure was logged.
- The app accepted every reported frame to the writer, released all pending GPU
  and encoder work, and retained at most 2.34 MiB of reusable readback storage.

The acceptance script uses native window messages for repeatable right-drag
input. The camera pose is logged when the input is handled and for each accepted
capture. The MP4 inspector checks each decoded timestamp against those capture
clock values. This measures input-to-captured-pose latency; it does not measure
physical-display scanout latency.

Machine-readable report, video hashes, full commands, and logs are under
`bin/CapturePort/realtime-camera-pressure-20261014-acceptance/report.json`.
