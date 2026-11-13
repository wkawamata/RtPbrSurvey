# PT Warm Glass Edges

Open **PT Warm Glass Edges** from Saved Scenes. This fixture extends the accepted
**PT Warm Glass Thin Shadows** scene. The camera, lighting, Studio environment,
exposure, seed and optical modes are retained; the four glass meshes are replaced.

- Tumblers: 128 radial segments (the control uses 48), a 16-segment semicircular
  rolled rim, smooth tangent normals, and rounded base transitions. Top height is
  1.65, outer lip radius 0.49; upper wall widths remain 0.036 and 0.132. Base
  thickness remains 0.11 and 0.28. Both tumblers still use thin-surface optics.
- Thin panel: 1.02 x 1.82 x 0.026, with a 0.004 rounded edge radius.
- Solid slab: 1.03 x 1.82 x 0.30, with a 0.025 rounded edge radius. It remains the
  single solid Volume object, IOR 1.5, no absorption.
- Rounded panels use three angular steps per quarter edge and tangent planes at
  each corner. Dimensions are authored
  into glTF positions; scene scale is one, preserving the physical edge radius.
  The slab placement is baked to avoid a second float transform of small faces.

The generator is `Tools/generate_glass_edges.py` (Python standard library).
Regenerate from the repository root with:

```powershell
python Tools/generate_glass_edges.py
```

It checks finite unit normals, positive signed volume, and exactly two oppositely
oriented incidences per welded geometric edge. Pole and face-seam vertex duplicates
are intentional. A denser comparison can be generated outside Assets:

```powershell
python Tools/generate_glass_edges.py --output build/glass-edges-dense --segments 256 --bevel-subdivisions 4
```

## Review

Use Native PT with Accumulate ON and wait for convergence. Compare the rolled
mouth of each tumbler and the reflected strip on each panel edge with the thin
shadow control. Check with Wireframe > Show current to distinguish the edge
geometry from a reflection or reconstruction artifact. A bevel creates a real
change of surface normal; increasing triangle count alone does not brighten a
flat face.

Then enable RR with thin/smooth-volume/transmission-motion experiments and native
SDK evaluation, and move/stop the camera. Check that narrow reflections remain
stable. This fixture does not implement hollow Volume optics, rough transmission
shadows, thick-slab transmission shadows or caustics. Visible thickness in the
thin tumblers is geometric and must not be interpreted as correct volume transport.
