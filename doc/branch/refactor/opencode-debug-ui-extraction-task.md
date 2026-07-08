# OpenCode Task: Extract SampleApp Debug UI Toward App Ownership

## Objective

Extract `SampleApp::DrawDebugUi` toward an App-owned debug UI component without changing runtime behavior.

The goal is to reduce `SampleApp.cpp` size and clarify ownership:

* UI is App-owned.
* Engine should expose state and commands, but should not own ImGui layout.
* Renderer/DX12 code should not gain new ImGui dependencies.

This is a medium-sized refactor. Keep it reviewable and avoid changing UI behavior.

## Workspace

```txt
C:\work\DirectX-Graphics-Samples\Samples\Desktop\D3D12HelloWorld\src\RtPbrSurvey
```

## Policy Documents

Read these before editing:

```txt
doc\branch\refactor\engine-separation.md
doc\branch\refactor\dxsample-split-inventory.md
```

## Current Context

Important current files:

```txt
SampleApp.h
SampleApp.cpp
D3D12HelloTexture.h
D3D12HelloTexture.cpp
ImGuiWidgets.h
Ui\ImGuiSystem.h
Renderer\
Scene\
```

Relevant current functions:

```txt
SampleApp::InitializeImGui
SampleApp::UpdateUiFrame
SampleApp::DrawSceneSelectUi
SampleApp::DrawDebugUi
```

`SampleApp::DrawDebugUi` currently owns a large amount of ImGui layout and app control logic. It is App behavior, not Engine behavior.

## Desired Direction

Introduce an App-owned debug UI extraction. Prefer a small first step over a large architecture rewrite.

Suggested target shape:

```txt
App\
  DebugUi.h
  DebugUi.cpp
```

or, if adding `App\` is too broad for this step:

```txt
DebugUi.h
DebugUi.cpp
```

Prefer `App\DebugUi.*` if the project file/filter change is straightforward.

## Preferred First Implementation

Use a **thin extracted function/component** first.

Recommended approach:

1. Add an App-owned debug UI helper.
2. Move the body of `SampleApp::DrawDebugUi` into that helper with the smallest practical interface.
3. Keep `SampleApp::DrawDebugUi` as a small forwarding method, or remove it only if call sites stay simple.
4. Preserve all existing ImGui controls, labels, defaults, enable/disable logic, and command behavior.

Acceptable first-step examples:

```cpp
namespace App
{
void DrawDebugUi(SampleApp& app, const RtPbrSurveyEngine::UiFrameContext& context);
}
```

or:

```cpp
namespace App
{
class DebugUi
{
public:
    void Draw(SampleApp& app, const RtPbrSurveyEngine::UiFrameContext& context);
};
}
```

Choose the smallest shape that compiles cleanly.

## Important Constraint

If moving the full body requires exposing too many private `SampleApp` members, do **not** make everything public casually.

Instead, choose one of these:

1. Keep `SampleApp::DrawDebugUi` as the public/private owner for now and extract only obvious subsections.
2. Add a narrow `SampleApp` private friend/helper interface if it is clearly better.
3. Stop at an inventory report explaining the required state surface.

Do not create a large public API just to make the move compile.

## Candidate Subsections

If moving the whole `DrawDebugUi` body is too broad, extract one or more focused sections instead:

* Camera UI
* Scene controls
* PBR lighting UI
* Environment map UI
* Material controls
* Tone mapping UI
* RayQuery shadow UI
* Hybrid reflection UI
* Render debug view UI
* Pixel pick UI
* Specular debug lines UI
* WorkMeter UI

Prefer extracting contiguous blocks with minimal state dependencies.

## Ownership Rules

### App-owned

These are App/UI ownership and may be moved to App debug UI code:

* ImGui layout.
* Scene selection controls.
* UI labels.
* App mode controls.
* User-facing debug controls.
* Calls that translate UI changes into app/engine commands.

### Engine-owned

Do not move these into App UI code:

* Renderer resource creation.
* Render pass execution.
* GPU command recording.
* D3D12 root signature or pipeline setup.
* Low-level ray tracing resource management.

The UI may call existing engine methods, but should not absorb engine implementation.

## Out Of Scope

Do not do these in this task:

* Do not redesign the UI.
* Do not rename labels or controls.
* Do not change ImGui layout behavior.
* Do not move renderer or engine implementation into App.
* Do not split `D3D12HelloTexture` render passes.
* Do not change scene loading behavior.
* Do not change command-line behavior.
* Do not delete `SampleApp`.
* Do not commit, push, merge, reset, checkout, or switch branches.

## Project File Requirements

If new files are added, update:

```txt
RtPbrSurvey.vcxproj
RtPbrSurvey.vcxproj.filters
```

If adding `App\`, add a Visual Studio filter such as:

```txt
Source Files\App
```

Follow existing project file style.

## Documentation Requirement

Update:

```txt
doc\branch\refactor\engine-separation.md
```

Add a commit-unit note explaining:

* What UI ownership boundary was clarified.
* Whether the full `DrawDebugUi` body moved or only subsections moved.
* Which files were added or changed.
* What state surface was exposed, if any.
* Verification results.

If the implementation is too risky and only an inventory is produced, create:

```txt
doc\branch\refactor\debug-ui-extraction-inventory.md
```

## Verification

Always run:

```powershell
git diff --check
```

If code changed, run Debug x64 build:

```powershell
& "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\amd64\MSBuild.exe" RtPbrSurvey.vcxproj /p:Configuration=Debug /p:Platform=x64 /m
```

Recommended runtime smoke check if practical:

```powershell
.\bin\x64\Debug\RtPbrSurvey.exe -AutoSelectGltfDamagedHelmet -LogToFile d3d12_debug.log -LogFPS 120
```

Let it run briefly, close it, then inspect:

```powershell
Select-String -LiteralPath d3d12_debug.log -Pattern "\[ERROR\]|\[WARNING\]|D3D12"
```

Do not commit generated log files such as `d3d12_debug.log`.

## Expected Report

Write a concise report with:

* Files inspected.
* Files changed.
* Whether full `DrawDebugUi` or only subsections were extracted.
* New App/UI owner shape.
* Any `SampleApp` state surface changes.
* Remaining UI extraction follow-ups.
* `git diff --check` result.
* Build result, if run.
* Runtime smoke result, if run.
* Final status: `done` or `blocked`.

