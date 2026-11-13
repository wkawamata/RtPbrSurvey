# PT Warm Glass Thin Shadows

Open PT Warm Glass Thin Shadows from Saved Scenes.

This review variant keeps the original Warm Glass Table camera and lighting.
Both tumbler assets and the thin panel use smooth thin transmission (roughness
zero). The panel is tinted cyan so transmitted direct light is easy to identify.
The thick Volume slab is unchanged and retains an opaque direct shadow.

Only analytic directional/point/spot direct shadows transmit in this step.
Non-delta rough thin glass, Volume refraction/absorption, environment visibility and emissive
NEE visibility keep their existing opaque-shadow behavior. This is not a caustic
implementation. The original Warm Glass Table materials remain unchanged.

The shadow weight at each smooth thin interface is
transmission * (1-metallic) * (1-Fresnel) * tint, using sampled material textures
and the geometric normal, consistently with the existing smooth thin branch.
A geometric sheet is crossed once, including on shared triangle edges. A closed
thin box has two distinct interfaces and deliberately multiplies twice, matching
the current camera-path model. Hollow tumblers can have four interface crossings.
This model is not a replacement for a future hollow Volume glass model.

Direct shadows are limited to sixteen thin interfaces and a final miss query.
The seventeenth interface blocks conservatively. In Rendering > Path Tracing
Output, Thin Shadow Status stores raw R=status bits (1 budget, 2 unsupported
rough/Volume, 4 invalid data/advance), G=max interfaces and B=shadow query count.
The channels aggregate analytic shadow calls along the traced camera path.
Use one sample/frame with Accumulate OFF and a raw SceneColor capture for numeric
flags/counts; otherwise they are averaged. The display is tone mapped.

A useful review is to compare the cyan panel shadow with the dark solid slab
shadow, then inspect the softer/lighter tumbler footprints and visible wood grain.
Use Native first. For RR, enable thin/smooth-volume/transmission-motion opt-ins
and SDK evaluation as in the original scene README.

A specialized PSO is selected only for thin-material scenes or this diagnostic.
The shared HLSL is compiled without the traversal in ordinary opaque scenes.
