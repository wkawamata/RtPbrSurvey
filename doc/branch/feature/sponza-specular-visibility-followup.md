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
