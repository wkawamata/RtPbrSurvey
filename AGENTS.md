# Agent Instructions for RtPbrSurvey

## Repository Context

- This repository is `RtPbrSurvey`, a standalone DirectX 12 / PBR / ray tracing survey application.

## DirectX 12 Conventions

- Follow the project's `.clang-format`.
- Do not reorder includes unless explicitly requested.
- Prefer `auto` only when the type is obvious.
- Use Allman braces.
- Use 4 spaces indentation.
- EOL should be CRLF.
- Use UTF-8 without BOM for text files unless an existing file clearly uses another encoding.
- Do not use emoji in any output or committed documentation; use text markers like `[x]`, `[ ]`, and `[*]` instead.
- Keep changes scoped and reviewable.

## Project Layout

- `App/`: application lifecycle, command line handling, scene selection, UI ownership.
- `Engine/`: renderer-facing engine orchestration and frame execution.
- `Renderer/`: render passes, pipeline/root signature helpers, GPU resources, debug render helpers.
- `Scene/`: scene data, sample scenes, glTF/object viewer scene definitions.
- `Shaders/`: HLSL shader sources.
- `Shared/`: cross-cutting data types shared by CPU/GPU or multiple modules.
- `Ui/`: ImGui system and UI helpers.
- `Assets/`: local runtime assets.
- `doc/`: branch notes, design notes, debug checklists, and review reports.

## Automation and Build References

Read these documents only when the related workflow is used:

- CLI automation flags: [doc/cli-flags.md](doc/cli-flags.md)
- D3D12 Debug Layer check: [doc/d3d12-debug-layer-check.md](doc/d3d12-debug-layer-check.md)
- Debug build command: [doc/build.md](doc/build.md)
- Native Windows automation: [doc/windows-gui-automation.md](doc/windows-gui-automation.md)

## Git Hygiene

- Check `git status --short` before committing.
- Do not stage generated files such as logs, build outputs, `.vs`, `bin`, `obj`, or temporary screenshots. Do not stage large external test assets unless explicitly requested.
- If the working tree contains unrelated user changes, leave them alone.
- Prefer small commits with clear intent.
- Before squash/merge/rebase operations, show the planned commit list when practical.
