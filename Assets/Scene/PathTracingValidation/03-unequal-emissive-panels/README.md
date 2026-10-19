# Unequal Emissive Panels

Two downward-facing panels at y=3 have sizes 2x2 and 1x1, centered at x=-2 and x=2.
Emission colors are (0.8,0.4,0.2) and (0.2,0.6,0.9). The area-selection ratio is
4:1, with four triangles in the GPU table. Analytic/environment lighting is off.

Compare BSDF-only, NEE-only and MIS; brightness and RGB balance should agree after
accumulation. The retained evaluation-results.json records independent quadrature,
scalar/RGB comparisons, paired seed checks and capture hashes for 64 spp/four seeds.
No numeric correctness claim is made for transforms edited after this saved state.
