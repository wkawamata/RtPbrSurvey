# PT Warm Glass Table

Open **PT Warm Glass Table** from Saved Scenes. Left to right, the scene shows
a thin-wall tumbler, a thick-wall tumbler, a thin sheet, and a thick slab on
a textured walnut tabletop. Three warm point lights serve as key, fill, and
rim. The vertical targets and tabletop grain make transmitted distortion
and surface reflections easy to see.

The hollow tumblers have wall widths of 0.036 and 0.132 scene units with
different base thicknesses. Both use thin-surface optics. The thin sheet uses
the same transmission model, and the 0.30-unit slab uses solid Volume with
IOR 1.5 and no absorption. The current Volume path permits one closed solid
in this scene. Use the slab to inspect solid refraction and the glasses to
inspect thin-surface reconstruction on meshes of different thicknesses.

To open the RR variant from the repository root:

```powershell
.ind\Debug\RtPbrSurvey.exe -SceneFile Assets/Scenes/WarmGlassTable/scene.json -PathTracingRrThinExperiment -PathTracingRrSmoothVolumeExperiment -EnableExperimentalNativeRayReconstruction
```

Debug verification at 960x540 produced Native and verified RR captures.
RR completed 18 outputs with one initial reset and zero input rejections.
Both runs reported zero captured D3D12 errors.
