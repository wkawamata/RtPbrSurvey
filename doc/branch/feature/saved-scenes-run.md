# Saved Scenes Runtime Entry

- Top Menu exposes Saved Scenes with Refresh and Run.
- Scene Editor and Top Menu share discovery under Assets/Scenes and
  Assets/Scene/PathTracingValidation, using immediate child scene.json files.
- Run uses the existing SceneFile runtime and the document's render preset,
  without editor controls or command-line rendering overrides.
- Debug is visible on entry, allowing PT and NRD diagnostic switching.
- Running and returning to Top Menu do not save the scene or its render preset.
- Load failures are displayed in Top Menu. Scene Editor remains the explicit
  editing and saving entry point.
- A locally edited scene is loaded as it currently exists on disk; it is not
  silently restored to a historical validation fixture.

Manual gate: Run 10-large-emissive-panel, select PT Radiance, one sample per
frame, four bounces, pixel-center primary and distance diagnostics. Compare
NRD ON/OFF for floor illumination, emitter-edge artifacts and motion trails.
This gate remains pending until confirmed by the user.

Validation, 2026-10-23: SDK-enabled Debug x64 build succeeded (22 existing
LNK4199 warnings, no errors). CTest passed 39/39, including shared scene
discovery, render preset, scene document, camera, capture and NRD GPU tests.
GUI Run and emissive quality still require interactive confirmation.
