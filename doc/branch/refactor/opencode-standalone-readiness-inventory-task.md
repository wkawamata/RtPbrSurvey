# OpenCode Task: Standalone Repository Readiness Inventory

## Objective

Audit what still prevents `RtPbrSurvey` from becoming a standalone repository while preserving history.

This is an inventory task. Do not implement broad changes. The output should help the next Codex/OpenCode pass choose commit-sized tasks that move the project closer to extraction from `DirectX-Graphics-Samples`.

## Workspace

```txt
C:\work\DirectX-Graphics-Samples\Samples\Desktop\D3D12HelloWorld\src\RtPbrSurvey
```

## Final Goal

The final goal is to split this application from the wider `DirectX-Graphics-Samples` repository into its own standalone repository, while preserving code history.

The refactor direction is:

* Remove dependency on the sample framework as an owner.
* Keep useful DX12/RHI utilities, but make their ownership explicit.
* Keep App / Platform / Engine responsibilities separated.
* Make the project buildable after repository extraction.
* Avoid broad behavior changes during readiness cleanup.

## Output File

Create:

```txt
doc\branch\refactor\standalone-readiness-inventory.md
```

## Inspect

Inspect at least these areas:

```txt
RtPbrSurvey.vcxproj
RtPbrSurvey.vcxproj.filters
packages.config
vcpkg.json
Assets\
Shaders\
App\
Platform\
Engine\
Renderer\
Scene\
Shared\
Ui\
```

Also inspect root files such as:

```txt
Main.cpp
stdafx.h
stdafx.cpp
GltfLoader.*
CubeMesh.*
TextureSemantic.h
WorkMeter.*
MyDx12Utils.h
ImGuiWidgets.h
TiniGgltfImpl.cpp
```

## Search For Evidence

Use `rg` and include exact evidence in the report.

Suggested searches:

```powershell
rg -n "DirectX-Graphics-Samples|Samples/Desktop|RtPbrSurvey|RtPbrSurvey|D3D12HelloWorld|src/packages|packages\\|packages/" -g "*"
```

```powershell
rg -n "#include\s+[<\"]\.\.|#include\s+\".*\.\." -g "*.h" -g "*.cpp" -g "*.hlsl" -g "*.hlsli"
```

```powershell
rg -n "\$\(ProjectDir\)|\$\(SolutionDir\)|\.\.\\|\.\./|Assets\\|Assets/|Shaders\\|Shaders/" RtPbrSurvey.vcxproj RtPbrSurvey.vcxproj.filters packages.config vcpkg.json
```

```powershell
rg -n "DXSample|SampleApp|D3D12HelloTexture|Win32Application|DirectX-Graphics-Samples" -g "*.h" -g "*.cpp" -g "*.md" -g "*.vcxproj" -g "*.filters"
```

If a search is too broad because of generated/build folders, repeat it with appropriate `--glob` exclusions and document the exclusion.

## Report Format

Write `standalone-readiness-inventory.md` with this structure:

```md
# Standalone Repository Readiness Inventory

## Summary

## Current Good State

## Blocking Issues

| Priority | Issue | Evidence | Impact | Recommended Task |
|---|---|---|---|---|

## External Dependencies

## Path Assumptions

## Project And Binary Naming

## Root File Ownership

## Assets And Shaders

## Generated Files / Ignore Rules

## Recommended Next Commit-Sized Tasks

## Verification
```

## Things To Classify

Classify findings into:

* Must fix before extraction.
* Should fix before extraction.
* Can remain after extraction.
* Cosmetic or naming-only.

## Specific Questions To Answer

Answer these:

1. Would `git filter-repo --path Samples/Desktop/D3D12HelloWorld/src/RtPbrSurvey/ --path-rename Samples/Desktop/D3D12HelloWorld/src/RtPbrSurvey/:` leave the project buildable?
2. Which project paths would break after extraction?
3. Are there references to parent repository package folders such as `src/packages`?
4. Are `Assets` and `Shaders` fully inside the extracted path?
5. Are generated files or logs currently tracked or likely to be committed accidentally?
6. What project/executable names still say `RtPbrSurvey`, `D3D12HelloWorld`, or other sample-derived names?
7. Which root files still need ownership relocation before the repository feels standalone?
8. What are the next 3-5 safest commit-sized tasks?

## Out Of Scope

Do not do these in this task:

* Do not move large code blocks.
* Do not rename the project.
* Do not edit package dependencies unless a tiny documentation-only correction is needed.
* Do not delete generated files.
* Do not run `git filter-repo`.
* Do not commit, push, merge, reset, checkout, or switch branches.

## Verification

Run:

```powershell
git diff --check
```

Build is optional because this is an inventory task. If no code changed, say build was skipped.

If code is changed despite the inventory scope, run Debug x64 build:

```powershell
& "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\amd64\MSBuild.exe" RtPbrSurvey.vcxproj /p:Configuration=Debug /p:Platform=x64 /m
```

## Expected OpenCode Report

Report:

* Files inspected.
* Searches run.
* Inventory file created.
* Top blockers.
* Recommended next commit-sized tasks.
* `git diff --check` result.
* Build result, if run.
* Final status: `done` or `blocked`.

