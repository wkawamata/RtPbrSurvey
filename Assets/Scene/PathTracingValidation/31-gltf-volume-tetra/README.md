# glTF Convex Volume

Small original embedded tetrahedron fixture; no external model dependency.
One mesh node, one primitive, one smooth white dielectric boundary material.
KHR_materials_volume selects solid transport; actual ray distance drives absorption.

Inspect refracted checker background and blue distance-dependent attenuation.
No quality approval is implied by a successful automated capture.
Unsupported: multiple solids/primitives/materials, concavity, nested/intersecting
objects, camera inside/on boundary, rough volume, textures, caustics qualification.
Moving the camera inside disables PT dispatch with a runtime status message.
glTF material values are asset-owned; the primitive material editor does not edit them.

Regression target: thin silhouette chords must not be skipped by origin offset
or ray TMin. Internal rays now start without that offset and reject same-boundary
self hits instead. The fixed-scene strict report requires zero missed exits and
includes a bias/TMin x100 control. Historical baseline reported 81 missed exits.
This does not guarantee robustness for every microscopic or grazing configuration.
