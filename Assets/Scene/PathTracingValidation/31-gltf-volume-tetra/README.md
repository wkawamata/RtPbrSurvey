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

Known numerical limit: at silhouettes, a chord shorter than origin offset plus
ray TMin can miss its exit surface. The RAW integration report counts these pixels
and independently bounds them to the analytic grazing band; interior errors fail.
