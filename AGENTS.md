# Agent Instructions for RtPbrSurvey

## Repository Context

- This repository is `RtPbrSurvey`, a standalone DirectX 12 / PBR / ray tracing survey application.
- It has been separated from Microsoft DirectX-Graphics-Samples. Do not assume the old `HelloTextureModified` or `DXSample` directory layout exists.
- Current workspace root: `C:\work\RtPbrSurvey-work`.
- Treat Microsoft sample-derived code as historical background only; new work should follow the standalone repository structure.

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
- Do not commit generated logs, build outputs, `.vs`, `bin`, `obj`, or local editor state.

## Project Layout

- `App/`: application lifecycle, command line handling, scene selection, UI ownership.
- `Engine/`: renderer-facing engine orchestration and frame execution.
- `Renderer/`: render passes, pipeline/root signature helpers, GPU resources, debug render helpers.
- `Scene/`: scene data, sample scenes, glTF/object viewer scene definitions.
- `Shaders/`: HLSL shader sources.
- `Shared/`: cross-cutting data types shared by CPU/GPU or multiple modules.
- `Ui/`: ImGui system and UI helpers.
- `Assets/`: local runtime assets. Large external test assets should be handled deliberately and not committed accidentally.
- `doc/`: branch notes, design notes, debug checklists, and review reports.

## CLI Flags for Automation

Flags are parsed in `RtPbrSurveyApp::ParseCommandLineArgs()`.

| Flag | Example | Description |
|------|---------|-------------|
| `-LogToFile` | `-LogToFile d3d12_debug.log` | On startup, dump accumulated D3D12 Debug Layer messages via `ID3D12InfoQueue` to the specified file. Also append new messages as they arrive when polled each frame. |
| `-LogFPS` | `-LogFPS 60` | Log CPU FPS to the same log file every N frames. Only active when `-LogToFile` is also specified. Value is `1000.0f / cpuFrameTimeMs`. |
| `-AutoSelectGltfDamagedHelmet` | `-AutoSelectGltfDamagedHelmet` | On startup, automatically select the `glTF Viewer > DamagedHelmet` scene and switch to Running mode without user interaction. |

## D3D12 Debug Layer Check

Use the Debug build with CLI automation when checking whether a change introduced D3D12 Debug Layer errors.

From repository root:

```powershell
.\bin\x64\Debug\RtPbrSurvey.exe -AutoSelectGltfDamagedHelmet -LogToFile d3d12_debug.log -LogFPS 120
```

Recommended check:

```powershell
Select-String -LiteralPath d3d12_debug.log -Pattern "\[ERROR\]|\[WARNING\]|D3D12"
```

For automated verification, start the exe, let it run for a few seconds, close it, then inspect `d3d12_debug.log`.
An empty log, or a log without `[ERROR]`, means no D3D12 Debug Layer error was captured for that run.
Do not commit generated log files such as `d3d12_debug.log`.

## Build Notes

Preferred Debug build command from repository root:

```powershell
& "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\amd64\MSBuild.exe" RtPbrSurvey.vcxproj /p:Configuration=Debug /p:Platform=x64 /m
```

If the solution or project file changes, verify the actual project name before reusing old commands.

## OpenCode Delegation

When delegating work to OpenCode:

- Editing target/workspace: `C:\work\RtPbrSurvey-work`.
- Task/report/log coordination folder should live outside the repo, typically under `C:\work\RtPbrSurvey-agents` unless a new RtPbrSurvey-specific agents folder is introduced.
- Always distinguish the edited workspace from the task folder in reports.
- Require CRLF and UTF-8 without BOM in task instructions.
- Require a machine-readable final status line such as `Status: done` or `Status: blocked`.
- Do not ask OpenCode to commit, push, merge, reset, checkout, or switch branches unless explicitly requested.

## Git Hygiene

- Check `git status --short` before committing.
- Do not stage generated files such as logs, build outputs, `.vs`, `bin`, `obj`, or temporary screenshots unless explicitly requested.
- If the working tree contains unrelated user changes, leave them alone.
- Prefer small commits with clear intent.
- Before squash/merge/rebase operations, show the planned commit list when practical.
