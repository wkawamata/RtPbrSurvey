# Scene Editor Live Camera Controls

Date: 2026-10-11. NRD work remains paused for integrated visual review.

## Behavior

- Scene Editor's title shows the current scene folder (for example,
  10-large-emissive-panel). Unsaved documents use their scene name. The ImGui
  window ID remains stable when switching scenes.

- Editable Position, Rotation XYZ (degrees), and vertical FOV operate on the
  current runtime camera. Changes apply without Enter/field deactivation,
  Reset, scene rebuild, or scene save.
- Mouse/keyboard changes appear in the same live controls on subsequent UI
  frames. Rotation is derived from the actual forward/up basis, not stale
  serialized rotation values. X is pitch, Y yaw, Z roll.
- Position edits translate the gaze point as well, preserving orientation.
- Rotation edits update the internal gaze point and up vector. Target is not
  exposed as an editable field, but remains part of the existing scene format
  and view-matrix contract. No scene JSON schema migration is required.
- Scene Camera Position/Rotation/FOV are static text and remain unchanged by
  live edits or mouse/keyboard navigation.
- Reset Camera restores the scene document camera to the live camera. It does
  not rebuild scene resources or change the document.
- Update Scene Camera copies the live camera into the scene document as one undoable
  edit. It includes position, target/up, FOV and projection parameters.
- Save Scene is still required to persist the updated document to disk. Live
  editing alone does not mark the scene document modified.
- Update Scene Camera text is amber when the live camera differs from the scene
  camera. Compare rendered orientation (forward/up), not gaze-point distance or
  Euler angle representation; allow small floating-point tolerances. Reset or
  Update removes the highlight once the camera matches.
- Save and Save As text is amber whenever the scene session has unsaved changes,
  including camera Update, node/material/environment/description edits. A
  successful save clears the highlight; a failed save leaves it highlighted.
- Ordinary node/material/environment rebuilds retain the current camera,
  including its roll. Camera orientation remains synchronized with FreeLook.
- Existing camera-change detection invalidates PT accumulation/reflection
  history; this UI does not implement another history/reset policy.

## Verification

Automated coverage: Euler/basis round-trip including roll and exact vertical
views; translation preserves orientation; existing camera/controller tests.
Debug build and final test results are reported with the change.

Manual checklist (not yet marked passing):

- [ ] Edit Position while PT accumulates: immediate movement and reset.
- [ ] Edit Rotation X/Y/Z and FOV: immediate angle/roll/zoom change.
- [ ] Move using mouse/keys: live numeric controls follow the rendered camera.
- [ ] Live edits leave static scene values unchanged.
- [ ] Reset restores the displayed scene values and camera angle.
- [ ] Update changes static values; Save Scene then reload restores that view.
- [ ] Node/material edits preserve position, orientation and roll.

Deferred/PathTracing switching was already confirmed manually by the user.
This change does not mark the other integrated visual checks complete.

## Automated Results

Debug x64 app and tests built successfully. The final parallel ALL_BUILD attempt
hit an Assets-directory copy collision in Mp4EncoderTests; that target succeeded
when retried alone. CTest: 28/28 passed, including the added rotation/translation,
pole round-trip and mouse/keyboard roll tests. One existing Streamline delay-load
link warning appeared in a camera test target. No NRD source changes were made
as part of this camera change. New UI click behavior and D3D12 runtime checks
remain manual/pending; automated math tests are not a substitute for those.

Build logs: C:/work/RtPbrSurvey-agents/scene-camera-live-edit-20261011,
scene-camera-live-edit-final-20261011 and scene-camera-mp4-copy-retry-20261011.
New executable: C:/work/RtPbrSurvey-work-3/build/Debug/RtPbrSurvey.exe.
The already running bin/x64/Debug executable was left untouched.

Action-color update: Debug app and CameraViewTests builds passed with zero
warnings/errors; CTest 28/28 passed. Added comparison tests cover equal views
with different target distances, position/FOV/roll differences, and equal cameras.
Build logs: scene-camera-action-colors-20261011 and scene-camera-match-tests-20261011
under C:/work/RtPbrSurvey-agents. Highlight visibility/click flow remains a manual
check; no automated UI operation was performed.

## Selected Node Transform Window

Selecting a hierarchy node shows an independent, movable Transform window.
Position, Rotation XYZ (degrees), and positive Scale edit the node's local
transform. Numeric changes update the preview immediately while preserving the
current camera. Move/Rotate/Scale, step values, and the X-/X+/Y-/Y+/Z-/Z+
buttons are grouped in the Transform window. Scene information is followed by
a SubWindow section with a Transform Window checkbox. The title-bar close
button and checkbox control the same visibility flag, initially enabled.
Hiding or collapsing this window does not disable the scene gizmo.
Its Transform Gizumo checkbox independently enables or disables the draggable
gizmo (initially on). Numeric edits and axis step buttons remain available when
the gizmo is off. The existing noninteractive selection-axis overlay is separate.

The vendored ImGuizmo renders a draggable local-axis gizmo over the scene using
the current, unjittered camera matrices. Mouse interaction with the gizmo does
not also manipulate the debug camera. World-space results are converted through
the parent's inverse transform. Nonrepresentable shear and nonpositive scales
are rejected rather than silently changing the hierarchy transform. One drag
is one undo entry; an edited numeric field commits on deactivation.

- [ ] Selecting another node updates the separate Transform window.
- [ ] Position/Rotation/Scale edits update the image without moving the camera.
- [ ] Move/Rotate/Scale changes the gizmo; dragging updates numeric values.
- [ ] A drag undoes as one operation; PT accumulation resets on scene edits.
- [ ] Parent transforms, perspective/orthographic cameras, Save/reload work.

Initial Debug build: zero warnings/errors. Existing CTest: 28/28 passed.
GUI interaction and D3D12 runtime verification remain pending/manual.
