# PT Warm Glass Table

Open **PT Warm Glass Table** from Saved Scenes. Left to right, the scene shows
a thin-wall tumbler, a thick-wall tumbler, a thin sheet, and a thick slab on
a textured walnut tabletop. Three warm point lights serve as key, fill, and
rim. The vertical targets and tabletop grain make transmitted distortion
and surface reflections easy to see.

The saved preset uses Procedural Studio with Environment Map / Importance MIS.
Studio is a distant environment. Two inward-facing emissive strips sit to the
left and right of the table, outside the saved camera view and on the table
side of the backdrop wall. The strips appear in glass reflections.

The hollow tumblers have wall widths of 0.036 and 0.132 scene units with
different base thicknesses. Both use thin-surface optics. The thin sheet uses
the same transmission model, and the 0.30-unit slab uses solid Volume with
IOR 1.5 and no absorption. The current Volume path permits one closed solid
in this scene. Use the slab to inspect solid refraction and the glasses to
inspect thin-surface reconstruction on meshes of different thicknesses.

To open the RR variant from the repository root:

```powershell
.\bin\x64\Debug\RtPbrSurvey.exe -SceneFile Assets/Scenes/WarmGlassTable/scene.json -PathTracingRrThinExperiment -PathTracingRrSmoothVolumeExperiment -PathTracingRrTransmissionMotionExperiment -EnableExperimentalNativeRayReconstruction
```

Debug verification at 960x540 produced Native and verified RR captures.
With the Studio setup, RR completed 24 outputs with one initial reset and zero input rejections.
Both runs reported zero captured D3D12 errors.

[Transmission and refraction roadmap](../../../doc/warm-glass-table-roadmap.md)

For interactive camera motion, keep Continuous RR ON. In Debug > Rendering,
expand RR Transmission Experiments and enable Thin glass RR, Smooth volume
glass RR, and Preserve RR during transmission motion. These experimental settings are included in Evaluation Cases Save/Apply.
The command above also applies them on startup.

During motion, Reconstruction output should remain rr after initial publication.
Completed should advance, while resets and cancelled stay stable unless inputs
are rejected or the camera projection/settings change. Per-frame conversion/SDK
timings can show not executed between evaluations; this alone is not a failure.
