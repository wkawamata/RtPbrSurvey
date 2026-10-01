# Observation: screen-fixed Path Tracing noise while moving the camera

Observed by the user on 2026-10-02 in the constant-environment floor fixture.

Reproduction: open `Assets/Scenes/PathTracingValidation/constant-environment/scene.json` using
`-SceneFile` and `-EnablePathTracing`, keep one sample/frame and fixed seed, then continuously rotate the camera.
The floor noise can appear fixed in screen coordinates. Stop moving and accumulation progresses.

Code evidence: `Shaders/shaders_PathTracing.hlsl::MakeRandomState` hashes pixel coordinates,
sample index and fixed random seed. `Engine/RtPbrSurveyEngine.cpp::SetCamera` invalidates history on
camera changes; `InvalidatePathTracingHistory` resets both accumulated sample count and frameSampleIndex to zero.
Thus every moving frame starts the same per-pixel random stream. Dispatch still covers every render pixel;
there is no random selection of a subset of pixels.

Proposal for the estimator owner: separate the accumulation counter/reset from RNG sequence progression
for interactive camera motion, while retaining explicit deterministic resets for static capture runs.
Specify manual reset, scene/settings changes and CLI capture semantics before implementation, and verify
static fixed-seed replay plus moving-camera variation. No estimator/engine/shader change is made in Part 2.
The observation does not invalidate static convergence measurements, whose sample index progresses normally.

Status: done
