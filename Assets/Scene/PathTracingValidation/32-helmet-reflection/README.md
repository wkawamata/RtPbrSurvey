# PT Helmet Reflection

Open this scene from Saved Scenes or Scene Editor. Use Native PT and wait for
accumulation before reviewing the visor-like area and surface reflections.
This is an opaque metallic-roughness asset, not a transmission/glass fixture.

The environment has a nonblack lower hemisphere. Direct lights and emission are
disabled to isolate environment lighting. Hybrid is not the reference and its
renderer is not modified by this investigation.

The source DamagedHelmet has a normal texture but no TANGENT attribute. The current
PT path ignores the normal texture in that case. Do not mistake the presence of
the texture in the asset for proof that the renderer uses it.

Run the diagnostic comparison from the repository root:

```powershell
python Tests/PathTracing/inspect_helmet_reflections.py --samples 64
```

Generated scenes, HDR captures, logs and report.json stay under bin. The script
compares lower-hemisphere lighting, 2/4/8 bounces and original/no-normal-map input.
It reports differences, not a quality pass. Equal normal-map A/B output confirms
the current missing-tangent limitation; a future fix must change that observation.
This fixed shot does not certify all glTF materials or convergence.
