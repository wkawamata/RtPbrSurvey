# MP4 Capture: Reusable Readbacks and Capacity Reservations

## Scope

Follow-up to [pipeline step 2](mp4-capture-pipeline-step2.md), after commit
`3113f15`. MP4 captures now use two fence-owned readback slots rather than one
fresh allocation per frame. Encoder threading and scheduler clock behavior
remain as established in that commit.

## Ownership and Admission

- Only the render thread submits requests, records copies, and maps readbacks.
- Before a request enters the screenshot queue, reserve one encoder frame and
  its future tightly packed RGBA8 byte size. Capacity includes reserved GPU
  work, queued CPU frames, and the executing encoder frame.
- The initial total capacity remains two frames and 128 MiB of reserved/owned
  RGBA8 data. A reservation is not an allocated CPU buffer.
- The fixed-step preflight checks a free GPU slot, empty submission queue, and
  worker capacity before advancing the pose. The same thread then reserves
  capacity when submitting the request; no other producer can consume it.
- MP4 request ownership detaches before recording the GPU copy. Later requests
  may use the second slot without releasing the first request's result ownership.
- Each slot records its submission sequence and fence. Polling retires slots
  in submission order, never maps a pending fence, and releases a slot only
  after conversion to an owned CPU buffer.
- Enqueue transfers a reservation to the CPU frame without double accounting.
  The encoder never touches a GPU resource or mapped memory.
- Readback buffers are reused when their allocation fits the next footprint.
  A larger footprint reallocates only a free slot. New sessions release prior
  readbacks so a previous large ROI does not retain memory unnecessarily.

GPU readback allocations are separate from the 128 MiB CPU reservation budget.
They are bounded by two aligned copy footprints for the frozen session ROI;
logs report their actual allocated bytes. PNG, GIF, EXR and diagnostic captures
retain single-in-flight synchronous output behavior and do not retain their
readback allocation after output completion.

## Stop and Failure

- Stop stops new admission, then drains all accepted GPU and encoder requests.
- Finalize waits for unfulfilled reservations. Already reserved frames may
  enqueue while the worker is finalizing; new reservations are rejected.
- A record/map/convert/enqueue failure cancels its reservation and completes
  the detached request exactly once on the render thread.
- Encoder failure, abort, and shutdown release unfulfilled reservations. These
  do not fabricate encoder results: GPU request results remain renderer-owned.
- A frame-size mismatch retains its reservation until explicit cancellation.
- Shutdown first waits for GPU completion, retires available copies, joins the
  worker, consumes results, and releases all remaining capture resources.
- Existing output resize failure-and-drain behavior is unchanged.

## Diagnostics and Validation

The GUI and `[CAPTURE_PIPELINE]` logs expose GPU pending count, readback bytes,
allocation count, and reuse count. `[CAPTURE_PIPELINE_END]` records final totals.
The reserved/owned count and bytes include GPU reservations, not only writer
queue entries.

- Worker tests exercise count/byte backpressure before pixels exist, duplicate
  reservation rejection, size mismatch, idempotent cancellation, reservation
  transfer, finalize waiting, append failure, abort, and shutdown cleanup.
- A WARP D3D12 test executes copies and waits for each fence, checks resource
  identity for same-size reuse, then checks reallocation for a larger ROI.
- The format validation script supports `--validate-readback-pool`: it checks
  two-frame/byte budgets, at most two allocations, observed reuse, and zero
  pending GPU/CPU reservations at completion, in addition to MP4 decoding.

## Verified Results

- CMake Debug and SDK-present standalone MSBuild Debug x64 succeeded.
- CTest passed 28/28. Screenshot/WARP and worker tests also passed ten repeats.
- Seven SDK-present GPU cases passed: PNG ROI, EXR sequence, GIF repeat,
  odd-size MP4 ROI, full-HD MP4, real-time MP4, and PathTracing MP4.
- Fixed-step videos retained every accepted frame and their expected PTS.
  Full-HD decoded 1920x1080, three frames at 30 FPS; odd ROI decoded padded
  64x48, five frames at 60 FPS.
- Full-HD used two readback allocations and one reuse, with 16,588,800 bytes
  allocated across the pool. The peak reserved/owned RGBA8 budget was also
  16,588,800 bytes (these are separate budgets).
- Real-time recording retained 59 frames over the requested 1.5 seconds, with
  two allocations and 57 reuses. This is not a sustained-60-FPS performance claim.
- All MP4 cases ended with zero GPU pending frames and zero reserved/owned
  encoder frames/bytes. All respected the two-frame and memory limits.
- No captured D3D12 ERROR/CORRUPTION or capture save error was reported.

Evidence: `bin/CapturePort/mp4-readback-pool-20261014/report.json`. Its commit
field is the starting commit `3113f15`; executable/inspector hashes identify
the working-tree binaries. Generated artifacts are not committed. A later GUI
text-wrapping-only adjustment does not change capture behavior.

These runs use a static scene. Logged pre-submission snapshots observed at
most one already-pending GPU copy; two simultaneous pending fences were not
forced by this integration test. Moving-camera validation, long pressure runs,
and GUI resize/restart validation remain separate acceptance work.

## Next

Separate live camera input/preview from deterministic capture simulation, add
a camera track evaluated at logical frame time, and validate moving-camera PT
and longer encoder-pressure recordings. Render-thread RGBA conversion remains
synchronous in this step and is a candidate for profiling/optimization.

This step does not claim sustained 60 FPS or complete camera-clock separation.
Fixed-step generation still intentionally waits when bounded capacity is full;
real-time mode advances live time and counts missed capture intervals.
