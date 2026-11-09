# Sponza Specular Environment Visibility Follow-Up

## Controlled observations (2026-11-06)

- Scene: opaque-only Sponza fixture (MASK primitives excluded).
- Camera, Procedural Studio environment, ACES exposure 16 are matched.
- Direct lighting, emissive lighting and Hybrid Reflection are disabled.
- Diffuse visibility: 64 cosine-weighted rays with spatial filtering.
- Native PT reference: 512 spp, maximum 8 bounces, no denoising.
- Generated images are local validation outputs, not committed assets.

Outputs under `bin/PathTracingValidation/`:

- `sponza-matched-exposure16-20261106/comparison-vertical.png`
- `sponza-ibl-components-20261106/comparison-vertical.png`
- `sponza-pt-exposure-20261105/16/capture.png`

Diffuse-only visibility ON retains the fabric base colors. Specular-only
reproduces most of the blue-white appearance of the combined result. This
isolates a dominant contribution; it does not validate diffuse visibility or
establish that the PT implementation is ground truth.

## Existing implementation

- `Shaders/PbrLighting.hlsli`: `ComputeSpecularOcclusion` uses material AO,
  roughness and N dot V. It does not query scene geometry.
- `Shaders/shaders_LightPass.hlsl`: diffuse visibility multiplies only diffuse
  IBL. Specular IBL still samples the prefiltered environment behind occluders.
- Hybrid Reflection is currently added to IBL. Enabling it is not a replacement
  or occlusion fix and may introduce overlapping energy.
- `Shaders/shaders_DiffuseEnvironmentVisibility.hlsl` uses the mapped normal for
  both origin offset and sampling. Self-intersection remains an independent
  unresolved diagnostic, especially on strongly normal-mapped fabric.

## Proposed validation increments

1. Add an opt-in specular environment visibility diagnostic, default OFF.
   Begin with mirror-direction binary visibility only as a geometry/debug
   check; do not present this as a rough-specular solution.
2. For rough surfaces, sample the specular lobe with a consistent PDF/BRDF
   weighting. Compare the environment-only contribution against a matching PT
   component, not against full PT with indirect surface reflections.
3. Keep specular and diffuse masks separate. A cosine-hemisphere miss fraction
   cannot represent visibility in a narrow reflection lobe.
4. Initially suppress blocked environment contribution without adding a guessed
   replacement. Clearly document that blocked rays receive no surface-reflected
   radiance in this diagnostic.
5. Treat replacing IBL with evaluated hit/miss reflection radiance as a separate
   compositing change. Avoid adding both representations of the same path.

## Acceptance checks before adopting a rendering change

- Open environment: enabling the diagnostic must preserve unoccluded specular.
- A wall blocking the reflection direction must suppress that environment path.
- Validate mirror and rough surfaces separately, including grazing views.
- Isolate mapped-normal self-intersection using bias and geometric-normal tests.
- Compare matched camera/environment/exposure; retain exposure 1 images as well
  as exposure 16 inspection images to avoid hiding errors with highlight clipping.
- Verify camera motion, D3D12 Debug Layer, standalone and host builds/tests.
- No default-setting change based solely on the current Sponza screenshots.

## Initial Mirror Diagnostic

An opt-in `Mirror IBL Occlusion (diagnostic)` control is available under Hybrid
Reflection. It defaults OFF and is saved as
`hybridReflection.mirrorEnvironmentOcclusionEnabled`.

It requires Deferred, RayQuery, Hybrid Reflection enabled, stochastic sampling
OFF, and an opaque-only scene. For roughness <= 0.1 it suppresses prefiltered
environment specular where the existing mirror ray has a geometry hit. Other
pixels are unchanged. The existing material gate and ray distance/bias settings
still apply. It does not trace a new visibility pass, filter the mask, provide
reflected hit radiance, or change the existing additive contribution behavior.

This is not rough-specular visibility: a single mirror ray cannot represent a
broad reflection lobe. In particular it is not a solution for Sponza fabric.
MASK, transmission and volumes remain unsupported by this diagnostic. Future
rough-lobe visibility and replacement compositing must be validated separately.

Validation: CMake Debug standalone and settings-test builds passed, including
shader compilation and CPU/HLSL constant-buffer layout assertions. Settings
round-trip CTest passed. Cornell ON/OFF captures (80 warmup frames, no reflection
contribution) produced no D3D12 errors. The 10,130 changed pixels were confined to
the mirror sphere (x=357..479, y=298..421); no pixels brightened. Open-environment
directions remained visible while blocked directions became black. This does
not validate rough lobes, camera motion, MASK scenes, or reflected-hit radiance.
Local evidence: `bin/PathTracingValidation/specular-mirror-occlusion-20261107/`.

## Rough-Lobe Diagnostic

`Rough IBL Occlusion (diagnostic)` is a separate opt-in setting, default OFF.
For roughness > 0.1 it evaluates up to 16 deterministic per-pixel GGX samples
using the existing Hybrid Reflection ray bias and range. It multiplies the
prefiltered specular IBL by the ratio of visible to total BRDF/PDF weight.
The ratio assumes constant incident radiance and dielectric F0; it is not an
RGB directional-environment integral or a replacement for reflected geometry.
It is evaluated independently of the material gate for the existing hit ray.

Requires Deferred, Hybrid Reflection, RayQuery/TLAS and enabled specular IBL.
On a MASK scene, `Force ON (MASK)` enables the rough diagnostic without
removing geometry or changing raster alpha coverage. The diagnostic rays treat
MASK holes as opaque. The override is saved in the scene settings, defaults
OFF, and does not enable transmission/volume scenes. The mirror diagnostic
can be enabled separately on MASK scenes with this override (see below).
The output uses the otherwise unused emission alpha
channel when enabled; the emission RGB and legacy payload when disabled are
unchanged. No new render target is allocated. The fixed sample sequence is not
temporally filtered; noise, grazing behavior, and motion remain to be qualified.

Matched opaque-only Sponza captures at exposure 16 (Diffuse visibility ON,
mirror occlusion and reflection contribution OFF) restored visible red, blue
and green cloth colors, but retained dark arches and visible specular noise.
No D3D12 errors were logged. Hybrid Reflection pass median was 6.64 ms OFF
and 10.07 ms ON (960x540 Debug, four measurements per mode; exploratory only).
Local evidence: `bin/PathTracingValidation/sponza-rough-ibl-occlusion/`.

### Residual Blue Highlights

On original Sponza, keep direct light and reflection contribution OFF, rough
occlusion and Force MASK ON, and match camera and exposure (16). Compare
Specular IBL ON/OFF PNGs with native `GBuffer.PBRParams` and
`ReflectionRayEmission` captures (`-DebugPreviewResource <name>` and
`-CapturePath <file>.ptbuf`). In Deferred Lit with the rough diagnostic active,
the latter's alpha is the visibility for roughness > 0.1.

Of 43,314 candidate pixels (8-bit blue increase >= 24 and at least 8 greater
than red increase), 2,009 had roughness <= 0.1; 41,305 had roughness > 0.1
and nonzero visibility; none in the latter roughness range had zero visibility.
This is a display-space diagnostic of one camera, not an HDR energy metric or
proof that all surviving directions carry the correct radiance. The scalar mask
can leave light from a blocked bright direction because its visibility samples
are not weighted by the environment radiance at the sampled direction.
Local evidence: `bin/PathTracingValidation/sponza-specular-visibility-attribution/`.

### Environment Luminance Weighting Experiment

The opt-in `Weight by Environment Radiance` diagnostic multiplies each GGX
sample's weight by the luminance of the environment in that direction. It
still scales the prefiltered RGB result by a scalar; it does not reconstruct
RGB radiance along the visible rays. Default remains OFF.

For the fixed 9,266 green pixels (near-white in Specular-only at exposure 16,
roughness > 0.1, nonzero visibility), the weighted variant retained 9,152
near-white pixels. Display-space mean Y increased from 237.59 to 239.67 / 255.
The unweighted capture was pixel-identical to the prior baseline, and neither
final capture logged D3D12 errors. This approximation is not a fix for the
highlights. Next compare visible per-direction RGB integration and validate
whether the bright directions are genuinely visible before attenuating them.
Local evidence: `sponza-specular-visibility-attribution/radiance-weighted-final/`
under `bin/PathTracingValidation/`.

### Visible Environment RGB Experiment

`Integrate Visible Environment RGB` is opt-in and replaces the rough split-sum
Specular IBL term with an estimate from the same 16 GGX samples. A miss uses
the environment RGB at the ray direction, weighted by the material's specular
BRDF/PDF; a hit contributes zero. Divide by all 16 samples, including hits and
invalid directions. Material specular AO and IBL intensity are applied as
before. Hit emission RGB is unchanged; the estimate has its own render target.

Original Sponza, Specular-only, Force MASK ON, exposure 16: the scalar baseline
matched the previous capture pixel-for-pixel. In the fixed 9,266 green pixels,
near-white coverage fell from 9,266 to 3,823, and display-space mean Y fell from
237.59 to 117.73 / 255. No D3D12 errors were logged. The floor still shows
sampling noise; this is one static comparison, not PT equivalence or motion
qualification. Some change may be due to replacing the split-sum BRDF estimate
itself. Local evidence: `bin/PathTracingValidation/sponza-specular-visibility-attribution/visible-rgb/`.

### RGB Spatial Filter

The optional `RGB Spatial Filter` applies a 5x5 spatial kernel only to the
integrated RGB. Taps are gated by material ID, roughness, metallic, surface
plane distance and world distance, then weighted by screen distance and normal
agreement. Default OFF; no ray or temporal-history change.

Original Sponza, Specular-only, Force MASK ON, exposure 16: filter OFF matched
the previous RGB capture pixel-for-pixel. In floor ROI (400,440)-(560,530),
horizontal adjacent-pixel display-Y RMS decreased from 53.87 to 10.11 / 255;
mean display Y changed from 29.02 to 34.15. This measures local variation, not
error against ground truth. Near-white coverage in the fixed green ROI changed
from 3,823 to 3,623 pixels. D3D12 logs contained no errors. Larger-scale mottling,
boundary bias and temporal stability still require inspection. Local evidence:
`bin/PathTracingValidation/sponza-specular-visibility-attribution/rgb-spatial-filter/`.

### Mirror Occlusion with Force MASK

On MASK scenes with rough occlusion and Force MASK ON, enabling `Mirror IBL
Occlusion` adds one independent binary visibility ray for roughness <= 0.1.
The ray treats MASK holes as opaque and is independent of stochastic reflection
sampling and the hit material gate. The legacy opaque mirror path is unchanged.

On the same original Sponza camera, Specular-only at exposure 16 with RGB
integration and filtering held ON, near-white coverage in the fixed 1,668 red
pixels fell from 1,668 to 25. Mean display Y fell from 235.28 to 3.38 / 255.
The 9,266 green pixels were unchanged, and the OFF baseline matched the previous
capture exactly. No D3D12 errors were logged. This is not alpha-aware visibility:
Force MASK can still over-occlude leaves and chains. Local evidence:
`bin/PathTracingValidation/sponza-specular-visibility-attribution/mirror-force-mask/`.
