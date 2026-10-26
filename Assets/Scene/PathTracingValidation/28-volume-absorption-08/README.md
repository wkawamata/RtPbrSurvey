# Volume Absorption: Thickness 0.8

Smooth closed box with physical world thickness 0.8, IOR 1.5, white boundary,
linear attenuation color (0.25, 0.5, 0.8), attenuation distance 1 world unit.
Compare with scene29 at the same camera and material settings. The thicker box
should absorb more light. Background refraction is evaluated at both boundaries.

This is homogeneous absorption, not alpha opacity or surface tint. Internal
reflections increase the traveled distance and therefore the absorption.
Volume Transmittance displays absorption alone, excluding Fresnel and background.
For per-path numerical checks use CurrentFrameRadiance, not accumulated SceneColor.

Only one Cube/Sphere with the camera outside is currently qualified. Rough
volumes, nested media, glTF volume import and colored shadows/caustics are not
covered. NRD falls back to Native PT. Visual acceptance is separate from tests.

Generated with `volume_absorption_fixture.py --thickness .8`.
