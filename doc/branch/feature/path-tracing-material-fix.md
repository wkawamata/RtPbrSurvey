# Path Tracing material corrections

Base: merged PR #85 (01ea9af), following the ViewZ correction. Branch: codex/path-tracing-viewz-fix.

## Changes

Part 5 reproduced two material errors. Ray-hit Albedo decoded UNORM color with pow(color, 2.2), differing from the standard sRGB transfer function by up to 0.00834401 on the fixture. SceneDocumentBuilder reused the base-color texture for metallic/roughness, occlusion and emission. Its green channel scaled JSON roughness 0.37 to 0.272784; its blue channel also scaled metallic, its red channel changed occlusion, and its RGB introduced emission despite document materials having no emission property.

Ray-hit sampling (Path Tracing and Hybrid Reflection) and the deferred GBuffer now use the standard piecewise sRGB transfer function. Textures are still uploaded as UNORM; this is manual decoding, not an additional hardware sRGB decode. Color-space metadata controls mip generation as before.

Document materials bind one shared white linear data texture for metallic/roughness and occlusion, and one shared black color texture for emission. Each material retains its own sRGB-encoded base-color texture and scalar factors. White data preserves scalar factors and neutral occlusion; black emission contributes zero. Imported glTF texture bindings are unchanged. The shared ray-hit and GBuffer color conversion also applies to imported glTF colors.

The CPU regression test checks colored document materials against neutral linear PBR data and black emission. Native guide comparisons now expect the JSON roughness values, retain standard-sRGB expected colors, and report the historical gamma-2.2 difference separately. Existing Part 5 reports remain historical evidence and are not rewritten.

## Reproduction

```powershell
python -B Tests/PathTracing/validate_inputs.py --packages bin/PathTracingValidation/python-packages --base-commit 01ea9af --output bin/PathTracingValidation/material-fix-final --cases input-plane-Albedo-static,input-plane-NormalRoughness-static,input-shifted-Albedo-static,input-shifted-NormalRoughness-static,input-ortho-Albedo-static,input-ortho-NormalRoughness-static,input-marker-Albedo-static,input-marker-NormalRoughness-static,input-shifted-ViewZ-static,input-shifted-MotionVectors-moving,input-camera-transform-ViewZ-static
```

Raw buffers, logs and build output stay in ignored bin directories. Native guide measurements cover full-screen plane and foreground marker materials. Spatial color textures, GPU metallic/occlusion/emission guide captures, and cross-GPU behavior are outside this numeric cohort; neutral data/emission bindings are verified by the CPU test. Changing color decoding and removing implicit emission can change previous rendered appearance; validation scene/preset documents are kept unchanged.

## Measured results

Tested clean commit: 83ef9a55e9385b6fc4189e2f25e7567f07af693f. Debug x64 build, 31 Python tests and SceneDocumentBuilder CTest pass. Twelve GPU captures complete without D3D12 errors. [Machine-readable report](path-tracing-validation-results/material-fix-summary.json) retains command lines, hashes, camera metadata, GPU/driver and individual comparisons.

- Four standard-color Albedo cases pass; maximum channel error is 0.000445052, within the unchanged 0.0015 tolerance. Foreground marker material classification has zero mismatches.
- Four NormalRoughness cases pass the unchanged 0.001 tolerance. JSON roughness 0.37 and 0.8 yield half-float values 0.369873 and 0.799805 instead of the previously color-scaled values.
- The supplemental plane overrides only the base color with [.001,.003,.01,1]. Encoded bytes [3,10,25] exercise both sides of the sRGB threshold 0.04045. Maximum color error is 1.36869e-6 against standard decoding, below the preselected 2e-5 tolerance. Reproduce by copying input-plane scene/preset into bin/PathTracingValidation/material-fix-low-color, applying the recorded override, then executing the supplemental command in the summary. Compare the interior quarter-grid against the standard decoding of those bytes.
- Both shifted and translated/rolled ViewZ cases retain the prior passing errors (4.76837e-6 and 4.68456e-5). Moving MotionVectors retain maximum NDC error 3.07919e-5 and pass the established half-float precision bound. The original fixed 2e-5 threshold still fails and is retained as a diagnostic.

The three reproduced Part 5 follow-ups (ViewZ, Albedo decoding, document material texture bindings) are corrected on this branch. Historical Part 5 reports describe the pre-fix code.

Status: done
