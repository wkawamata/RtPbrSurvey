# Volume Absorption: Thickness 1.6

Same camera, boundary IOR and homogeneous absorption as scene28; thickness is
1.6 world units instead of 0.8. Compare the central transmitted checker colors.
The absorption-only transmittance squares when the actual path length doubles.
The final image also contains boundary Fresnel reflection and refraction, so its
RGB values are not simply the square of scene28's final display RGB.

See scene28's README for scope and diagnostic guidance.
Generated with `volume_absorption_fixture.py --thickness 1.6`.
