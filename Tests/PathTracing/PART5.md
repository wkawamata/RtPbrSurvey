# Part 5: PT input buffer validation

## Minimal reproduction and instrumentation proposal

Base: 9455eec; branch: codex/path-tracing-validation-part5.
Existing -DebugPreviewResource changes the inspector display, but PNG captures the composed display and PFM always captures normalized accumulation RGB. Neither preserves the selected guide channels. Proposed scoped instrumentation: a .ptbuf capture selected by the existing -DebugPreviewResource option; use the existing readback and fence, preserve native half/float values and all channels, and save the exact current/previous camera matrices in the header. No estimator or shader changes.

CameraState already supports lensShiftX/Y, but scene JSON rejects them and cannot reproduce PR83 off-center projection. Expose optional finite lensShiftX/Y in scene documents, default zero, and forward to the existing camera implementation. No projection algorithm or camera API change.

Potential ViewZ issue to measure: ComputePrimaryViewZ obtains forward from the inverse-projected NDC center at far Z. With lens shift, this is an off-axis ray instead of the camera view axis. A front plane at view distance 5 can therefore have spatially varying ViewZ. Compare saved values against both camera-axis depth and that shader direction; report the discrepancy without fixing the shader.

## Capture automation reproduction

The initial 20-run cohort in bin/PathTracingValidation/part5-inputs preserves four requested moving captures that did not move: -SceneFile input-plane/scene.json -ReflectionOrbitDegrees 8 -ReflectionOrbitFrames 8 -CaptureAfterFrames 30 kept camera [0,0,-5], target [0,0,0] and identical current/previous matrices. Do not count these as camera-motion evidence. OpenFileScene bypasses the Arcball initialization and CLI debug inspector opening performed for sample scenes. Minimal proposal: reuse the existing Arcball initialization in OpenFileScene and share the existing inspector-opening code, without changing camera math or shaders. Repeat the cohort in a fresh directory and require nonzero expected projected motion for requested moving captures.

## Pre-final precision policy

The retained orbit smoke (part5-orbit-smoke) moved the shifted camera to [-0.6958655,0,-4.9513402]. Motion agreed in sign and magnitude, but its max absolute error 3.0792e-5 NDC exceeded the initial 2e-5 threshold. The initial threshold result remains in every report. For the final cohort, also test each component against 2 ULP of its expected FLOAT16 value + 3e-6 NDC for float ray/geometry/matrix arithmetic. Half spacing scales with magnitude (3.05176e-5 around 0.032), so a fixed 2e-5 bound cannot cover that format across larger motion. This is an explicit conservative storage/computation bound, not an estimator change. Record max normalized error; any violation remains a failure. Static expected zero uses the same rule (~3.12e-6).

Albedo's standard-sRGB expectation and 0.0015 threshold remain unchanged. Separately compare the implemented pow(UNORM,2.2) definition against the same threshold. Preserve the standard-sRGB discrepancy instead of passing it by increasing tolerance.

## Definitions and expected values

All primary guides come from the first sample of the current frame, not the accumulated image. The sample index can reset to zero while the camera moves. The capture header records the actual index/seed used by the dispatch, exact current/previous/inverse VP matrices, camera position/target, dimensions and native DXGI format. PT disables temporal-upscaler camera jitter; primary rays still use seeded subpixel positions.

| Resource | Saved channels and definition | Independent expectation | Tolerance |
|---|---|---|---|
| NormalRoughness | RGBA16_FLOAT: world shading normal xyz, effective material roughness w | Front-facing plane normal (0,0,-1); base roughness .37 times texture G 188/255 = .2727843; marker .8 times 137/255 = .4298039 | .001 per channel |
| ViewZ | R32_FLOAT: max(dot(hit-camera, normalize(inverseVP NDC far-center-camera)),0) | True camera-axis depth 5 on the background, 4 on the foreground marker; separately evaluate the implemented off-axis direction | .0001 world units |
| MotionVectors | RG16_FLOAT: previous NDC xy - current NDC xy at the same primary surface, with previous object world transform | Independent CPU ray/plane intersection and projection through recorded current/previous matrices; zero for static camera/objects | Initial absolute 2e-5 retained; final per-component 2 half ULP + 3e-6 NDC |
| Albedo | RGBA16_FLOAT: pow(sampled UNORM RGB,2.2), alpha 1 for primary hit | Standard sRGB decode of the known texture bytes, and separately the implemented power approximation | .0015 per channel |

World is the repository's left-handed coordinate system. The plane's -90-degree X rotation transforms its object-space +Y normal to world -Z. Camera orbit changes the view basis but must not rotate this world-space guide normal. The main cohort measures moving motion buffers; an additional shifted-camera control measures moving normal and ViewZ. Static normals cover symmetric, shifted and orthographic projections. Normal mapping, curved-surface interpolation and nonuniform scale are outside this fixture's coverage.

Motion is in NDC, not UV or pixels. To obtain previous-minus-current pixel displacement, multiply x by width/2 and y by -height/2 (screen y points down). The comparison checks both signs and scale across the image. It uses the same current hit point for both matrices, without differencing independently jittered samples from successive images. Objects remain fixed; object animation/previous-world updates are not GPU-validated in this suite.

The editable marker scene has a 2x2 foreground plane centered at (.5,.25,-1) over the background at z=0. Its material differs from the background. Independent ray intersections choose the first hit and predict the material at every interior sampled pixel. This checks shifted-projection primary hit alignment in addition to matrix reprojection and camera-motion vectors.

Scene material generation encodes linear baseColor to rounded standard-sRGB bytes. It currently reuses that same texture for metallic/roughness/occlusion/emission. Consequently material roughness is not the literal scene JSON number. The report preserves this limitation; changing texture bindings belongs to a separate material fix. These scenes use no external assets or normal maps, maxBounces=1, one sample/frame, constant environment MIS (5), no direct lights, emission off, RR off, exposure 1 and auto-exposure off.

## Native capture contract and preview

Use lowercase .ptbuf with the existing -DebugPreviewResource selector for one of the four primary guides. Only the default request contract and a full-frame capture are supported. The existing asynchronous screenshot readback/fence is reused. The selected source is explicitly transitioned to COPY_SOURCE and restored to UAV; no shader or estimator changes are made.

The binary layout is ASCII `PTBUF1` plus LF, one UTF-8 JSON header plus LF, then tightly packed top-down interleaved little-endian native channels. DXGI 10 means four half channels; 34 means two half channels; 41 means one float32 channel. RowPitch padding is removed. Values are not exposed, clamped, gamma-converted, sample-normalized or tone-mapped. The Python reader converts stored half/float to float64 for analysis and rejects wrong sizes/nonfinite values. Matrices in the header are the transposed DirectX row-vector matrices: CPU projection multiplies homogeneous row vectors by the transpose of a stored header matrix.

PNG captures the composed window including the inspector/UI. Preview settings may apply normal mapping from [-1,1] to [0,1], channel selection, exposure, scale and offset. Motion uses a centered 32x display scale. ViewZ is registered as an ordinary scalar rather than device depth. Display settings and PNG hashes cannot establish native buffer values; all reported numeric comparisons use .ptbuf.

## Reproduction

From repository root, using Python with NumPy (matplotlib for plots):

```powershell
python -B Tests/PathTracing/validate_inputs.py --output bin/PathTracingValidation/part5-inputs-final
python -B Tests/PathTracing/validate_input_controls.py --input bin/PathTracingValidation/part5-inputs-final --output bin/PathTracingValidation/part5-controls
python -B Tests/PathTracing/summarize_inputs.py --output bin/PathTracingValidation/part5-inputs-final --controls bin/PathTracingValidation/part5-controls/report.json
.\Tests\PathTracing\Invoke-MotionVectorValidation.ps1 -SceneFile Assets\Scenes\PathTracingValidation\input-shifted\scene.json -RenderPreset Assets\Scenes\PathTracingValidation\input-shifted\render-preset.json -OutputDirectory bin\PathTracingValidation\part5-preview
```

`--packages` accepts an existing local package directory. Use fresh output directories. `--cases input-shifted-ViewZ-static` isolates the depth reproduction. The raw script logs and retains process failures, timeouts and D3D12 errors and rejects a requested moving capture with zero expected motion. `--analyze-only` recomputes metrics from captures; use it only with the same executable/source provenance as the saved cohort. Source/executable hashes, full commands and scene/preset hashes accompany each final result.

Numeric comparison failures also produce `status: incomplete` and a nonzero exit code, retaining the measured result in the failed record. NormalRoughness, Albedo and ViewZ use their declared numeric tolerance; Albedo also requires zero primary-hit material mismatches. MotionVectors use `halfPrecisionBoundPassed`, while the older absolute-threshold result remains a diagnostic. This policy applies to both new captures and `--analyze-only` runs.

Final plan: 16 static captures (four resources x four scenes) and four camera-orbit motion captures. Seed 7; captureAfterFrames 30; 8 degrees over the last 8 frames. Output dimensions are recorded per capture (1920x1080 on this machine). Numeric ROI is x/y in [16,dimension-16), stride 4; all full-buffer values must be finite. Initial smoke and nonmoving pilot remain under ignored part5-smoke, part5-inputs and part5-orbit-smoke, with their limitations preserved above.

## Retained preview failure and minimal correction

The first reuse of Invoke-MotionVectorValidation.ps1 with the shifted file scene produced two identical black PNGs (SHA A96C7A28999A45ADCDF91AC1124F507B0FDE24E285A4C00097CB2E7AF8FDDBE0), despite native moving motion being validated. OnInit forcibly sets m_debugUiVisible=false after OpenFileScene; the next UI update closes all inspector slots. Proposal: honor the existing enableDebugTexturePreview flag in that assignment, retaining hidden UI when no preview is requested. The failed captures, script log and incomplete controls report remain in part5-controls. Retry only preview and one native regression in fresh output directories; do not erase the failure.

## Results on this machine

GPU: NVIDIA GeForce RTX 3080 Laptop GPU; driver 616.64; Debug x64. Base 9455eec; numeric cohort tested db83005; explicit-preview correction and its PNG/native regression tested 1aa2749. Scene/preset/source/executable hashes and exact commands are in the committed part-5-summary.json. The two binaries differ in file-scene preview visibility; the post-fix native normal payload is identical to the cohort's payload.

| Check | Observed | Judgment |
|---|---|---|
| World normal / effective roughness | max channel error .00048828125, within .001; moving camera preserves world -Z rather than view-space (.139173,0,-.990268) | Matches implementation |
| Symmetric perspective ViewZ | 4.99999905 to 5.00000143, expected 5 | Pass |
| Orthographic ViewZ | 4.99999952 to 5.00000048, expected 5 | Pass |
| Shifted perspective ViewZ | 3.63780522 to 6.91837502 on the plane whose camera-axis depth is 5; max error 1.918375 | Camera-axis depth fails; implemented off-axis definition agrees within 5.65e-6 |
| Shifted marker hit alignment | zero wrong material classifications among 123,664 interior sample pixels; matrix reprojection error <=3.67e-6 NDC | Primary hit alignment passes |
| Stationary motion | max error 1.90735e-6 NDC (zero expected) | Pass |
| Camera motion | max error 3.07919e-5 NDC, <=.02957 pixels at 1920 width; all cases within pre-final per-component half-format bounds | Sign/unit consistent; shifted cases exceed retained initial absolute 2e-5 threshold |
| Albedo | (.2548828125,.51123046875,.75927734375), versus standard-sRGB (.250158285,.502886458,.752942217); max error .00834401 | Standard sRGB fails; gamma-2.2 implementation agrees within .000167351 |
| Fixed seed / different seed | same-seed native marker payload identical; seed 8 changes 651 boundary pixels, both sets match independently predicted materials | Pass |
| Additional moving ViewZ | camera-axis discrepancy max 2.3210244; implemented direction error <=6.54e-6 | Confirms depth-definition issue under movement |
| Existing capture routes | PFM parses with finite RGB; PNG preview retry differs and visually shows neutral static/colored moving inspector | Capture-route checks pass |
| Debug layer | zero D3D12 errors in final cohort, native controls, failed preview and successful preview retry | Pass |

The guide fixtures intentionally disable diffuse/specular IBL lobes and direct lights; their composed radiance can be black. This does not imply missed geometry: native albedo/normal/depth guides contain the independently verified hits. PFM here is only an export/finite-value regression, not a radiance correctness test. The initial black PNGs lacked the requested inspector; after the visibility correction the moving inspector displays the actual signed motion.

Build passed. Four C++ tests passed (SceneDocument, SceneDocumentBuilder, CameraProjection, Screenshot); 30 Python tests passed, including six native-guide tests. Existing MSVC macro/vcpkg import warnings remain. PNG/PFM/.ptbuf/log/CSV/plots are ignored, not committed. See bin/PathTracingValidation/part5-inputs-final for view-z-validation.png/.svg, motion-validation.png/.svg, metrics.csv and summary.json; controls and retained failures use the sibling output directories described above. The first failed preview report remains incomplete on purpose, while the summary separately records the successful retry.

## Follow-up proposals

1. ViewZ: use the true camera view forward, independent of lens shift. Either upload the existing camera basis forward to the shader or derive a consistently oriented near-plane normal from inverse-projected near-plane points. Choose this in the renderer workstream, then rerun the same shifted static/moving fixtures. Do not replace it with Euclidean hit distance, which is also different from view-axis depth.
2. Color space: agree whether gamma 2.2 is intentional. If exact standard sRGB is required, use its piecewise decode consistently in SceneRayQuery and GBuffer and compare these same known bytes. This affects rendering beyond PT and deserves a separate reviewed change.
3. Scene material bindings: use a neutral metallic/roughness texture rather than the baseColor texture if JSON roughness should be the effective value. Preserve dedicated occlusion/emission semantics as well. Changing builder bindings is outside this measured baseline.
4. Expand coverage to object motion/previous-world updates, normal-mapped and nonuniformly scaled geometry, spatial multi-texel textures, miss sentinels and other GPUs. Object animation is not validated here; no CLI object-motion interface was introduced.

The validation part is complete; the detected rendering/material differences are not fixed by this branch. No shaders, denoiser, NRD or DLSS RR integration were changed.


To regenerate this machine's summary including the retained failed preview and its post-fix checks, add `--preview-retry bin/PathTracingValidation/part5-preview-retry/path-tracing-motion-vectors.json --native-regression bin/PathTracingValidation/part5-preview-regression/report.json` to the summary command. Fresh reproductions on the corrected source use the controls runner's successful preview directly and do not require those historical retries. The controls runner also accepts `--powershell` for the local PowerShell 7 executable.

Status: done
