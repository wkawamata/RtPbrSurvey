# Emissive NEE Validation

The scene is an immutable starting point for the earlier opaque area-emission
comparison: a 10x10 receiver and a downward-facing 3x3 emitter at y=3.
There are no analytic lights; environment lighting is disabled. Emission RGB is
(0.8, 0.4, 0.2). The interactive preset enables PT, emission, shadows, two bounces,
Radiance output and NEE-only. Use Save As for additional evaluation variants.

Scene Editor > Refresh Scene List lists this folder. Description is a multiline
UTF-8 field recording what to inspect and what to confirm; Save persists it in
scene.json. Japanese text uses the existing Windows ImGui font and IME backend.

evaluation-results.json retains the earlier numeric results. It is not evidence
for later edited transforms or for MIS. Full capture hashes and provenance are
retained in the referenced document; raw PFM/log files are local generated outputs,
not archived by this asset. The archived starting scene matches the runner fixture.

evaluation-mis-results.json separately retains the 2026-10-05 three-mode basic
campaign. All independent-reference and paired-seed comparisons passed. This does
not establish correctness for multiple/unequal/textured emitters or deeper RR paths.
Use Emissive Sampling = MIS (BSDF + NEE) for interactive review of the same scene.

From the repository root, the original campaign can be regenerated in a new folder:

```powershell
python -B Tests/PathTracing/validate_emissive.py --output bin/PathTracingValidation/emissive-review-repeat --samples 64 --modes 0 1 --require-emitter-table --visibility-controls
python -B Tests/PathTracing/validate_emissive.py --exe build/Debug/RtPbrSurvey.exe --output bin/PathTracingValidation/emissive-mis-repeat --samples 64 --modes 0 1 2 --require-emitter-table --visibility-controls
```
