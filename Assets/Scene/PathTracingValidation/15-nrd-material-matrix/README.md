# NRD material matrix

Static input/composition fixture, not a general specular quality benchmark.

| Panel | Metallic | Roughness |
| --- | --- | --- |
| Upper left | 0 | 0.6 |
| Upper right | 0 | 0.1 |
| Lower left | 1 | 0.6 |
| Lower right | 1 | 0.1 |

All panels face the camera under a constant white environment. Distinct base
colors identify the four materials after document sRGB8 encoding and GPU decode.
The test checks captured F0/metallic/roughness, per-material NRD admission and
composition coverage, inactive diffuse on pure metals, reversible material
factors, final composition arithmetic and exact Native fallback. Missing panels,
incorrect values, missing resources and nonfinite data are rejected.

From the repository root, using the SDK diagnostic Debug build:

```powershell
python Tests/PathTracing/validate_nrd_material_matrix.py --output bin/PathTracingValidation/nrd-material-matrix-new
```

Use a new/empty output directory. The runner copies the source scene and preset;
it never edits them. It captures eighteen resources in separate deterministic
runs and requires matching frame metadata and successful RELAX recording.

The 0.1 roughness condition is not a perfect delta mirror. The uniform environment
does not test sharp reflected features, high dynamic range, motion, curved
geometry or subjective glossy quality. Passing this fixture must not enable NRD
by default or remove broader scene/material acceptance gates.
