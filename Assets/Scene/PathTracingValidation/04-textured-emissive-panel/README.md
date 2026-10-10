# Textured Emissive Panel

A downward-facing 3x3 panel at y=3 uses an embedded 2x2 black/white PNG and explicit
0-1 UVs. The sampler wraps and bilinearly filters before the shader's sRGB decode.
The reference integrator follows that ordering and splits quadrature at texture
filter boundaries. Emission color is (0.8,0.4,0.2), with two emitter triangles.

Compare BSDF-only, NEE-only and MIS. The retained evaluation-results.json includes
independent scalar/RGB reference and paired seed checks at 64 spp/four seeds.
UV transforms, different textures/filter modes and edited geometry are not covered.
