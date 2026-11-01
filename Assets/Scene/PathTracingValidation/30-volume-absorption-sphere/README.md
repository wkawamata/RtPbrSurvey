# Colored Absorbing Volume Sphere

One closed radius-1 sphere, IOR 1.5, white boundary, attenuation color
(0.25, 0.5, 0.8) and distance 1 world unit. Curved boundaries bend the rays;
the variable distance inside the sphere changes the transmitted color.

Inspect the central transmitted checker and the brighter edge reflection.
This is not a scattering/fog model. Colored light projected onto a receiver,
rough volume transmission and volume-compatible NRD remain future work.
The camera must stay outside. See scene28's README for diagnostics and scope.

Generated with `volume_absorption_fixture.py --sphere`.
