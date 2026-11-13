# RR glass rim guides and tighter evaluation geometry

## RR input correction

Baseline: main e1d1292. Glass transport samples geometric normals, but the primary
RR guide previously used interpolated normals. Pure dielectric thin/Volume
surfaces now provide the same geometric normal when using the RR sampling profile,
and F0 from ((IOR-1)/(IOR+1)) squared. Ordinary Native guides retain their previous
values; this does not add light, sharpen output or alter the path radiance.

SecondaryRayData.w now carries primary material transmission fraction [0,1] in
RR sampling profiles (zero otherwise). It is independent of the stochastic event:
a glass reflection and transmission both describe the same material. The RR
converter multiplies diffuse albedo by (1-TF), retaining specular albedo and
existing hit/miss distance conventions. Negative, >1 and nonfinite fractions are
rejected. No new texture or GPU pass is introduced. Legacy independent-probe
conversion still requires its original zero reserved value.

## Fixed-geometry comparison

Before changing meshes, captured the original round-rim geometry at 960x540,
fixed camera/seed, Native 512 accumulated samples and RR at 1/4/16 spp after 180
display frames. Repeated the RR cohort with corrected guides, using copied meshes.
Every RR run verified 36 completed outputs, zero rejected and zero cancelled.

Display RGB RMSE against Native in fixed upper-rim rectangles:

| spp | Thin before | Thin after | Thick before | Thick after |
|---|---:|---:|---:|---:|
| 1 | 0.02169 | 0.02100 | 0.02374 | 0.02369 |
| 4 | 0.01501 | 0.01404 | 0.01786 | 0.01666 |
| 16 | 0.01072 | 0.00936 | 0.01327 | 0.01115 |

Thin ROI=[182,150,300,184), thick=[337,148,453,188). These are display-image
observations, not linear-light energy or highlight-completeness acceptance.
The 1-spp improvement is small, especially on the thick rim. Fine specular
smoothing remains; do not call this full Native/RR equivalence or a complete
resolution of every missing highlight. More samples help this cohort, but the
scene default remains 1 spp; no performance acceptance or speed claim is made.

## Geometry requested by user

WarmGlassEdges thin-wall width changes 0.036 to 0.012. Both mouths now have flat
top lands and small inside/outside quarter-circle fillets instead of full-width
semicircular rolls. Fillet radii: thin 0.002, thick 0.006. Thick-wall width 0.132,
height, outer radius and base thicknesses are retained. Original WarmGlassTable
and WarmGlassThinShadows assets are unchanged. Both meshes have 13312 triangles.

Closed manifold/orientation/unit-normal/positive-volume generation checks and
whole-scene import/placement checks passed. Final new-geometry Native512, static
RR1/RR4 and automated RR camera orbit captures passed; all RR runs again verified
36 outputs with zero cancelled/rejected. These are smoke captures, not user
visual acceptance of the new rim appearance or interactive motion quality.

## Validation and evidence

CMake Debug app build passed with zero warnings/errors. WARP tests passed 40 live
RR vectors (including reflected/transmitted glass, mixed TF/metallic, IOR1 and
invalid fractions) and the existing 32 legacy conversion vectors, no D3D12 errors.
Accepted application capture logs contain no ERROR/CORRUPTION. Existing other GPU
applications were left running: no timing comparison was performed.

Local evidence under build/rr-rim-20261114: baseline-v2, guides-v1, sharp-rim-v1,
and guide-comparison.json. The initial absolute-path fixture failed schema import
and is excluded. Generated evidence is not committed. User visual review remains
required for upper-rim highlight retention with Smooth volume glass RR ON.
