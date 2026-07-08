# Standalone Repository Readiness Inventory

## Summary

The project is close to being extractable from the `DirectX-Graphics-Samples` repository. The major refactoring (DXSample removal, SampleApp→RtPbrSurveyApp rename, file moves into App/Platform/Engine/, RenderPass→FrameGraph) is complete. NuGet packages are now expected under the project-local `packages/` directory, so standalone checkouts need a local NuGet restore step.

## Current Good State

- **No `DXSample` references** in any source file (0 matches).
- **No `SampleApp` references** in any source file (0 matches).
- **No `D3D12HelloTexture` references** in any source file (0 matches).
- **All files moved** into App/, Platform/, Engine/, Renderer/, Scene/, Ui/, Shared/ subdirectories.
- **`Platform::IApplication`** host interface decouples Win32Application from the app class.
- **No dead planning docs** interfere with the build; `doc/` is not referenced by `.vcxproj`.
- **`git diff --check`**: clean (no whitespace errors).
- **`.gitignore`** exists and covers `vcpkg_installed/`, `packages/`, `imgui.ini`, `d3d12_debug.log`.

## Remaining Issues

| Priority | Issue | Evidence | Impact | Recommended Task |
|---|---|---|---|---|
| **Done** | Document project-local NuGet restore before standalone build | `.vcxproj` imports `packages\Microsoft.Direct3D.*` and `packages\WinPixEventRuntime.*`; `packages/` is ignored and should be restored locally. | This is a normal fresh-checkout build prerequisite, not an extraction blocker. | `BUILD.md` documents the external `nuget.exe` prerequisite; `Restore-NuGet.ps1` restores `packages.config` into project-local `packages/`. |
| **Should** | NuGet and vcpkg remain mixed package managers | `packages.config` exists alongside `vcpkg.json`. | Acceptable short-term, but dependency ownership remains split. | Keep this short-term for stable extraction; evaluate full vcpkg migration later. |
| **Should** | `$(ProjectDir)` added to AdditionalIncludeDirectories | `.vcxproj:76,108` — works but is a workaround for subdirectory source files | After extraction, `$(ProjectDir)` resolves to the new repo root, which is fine. | No change needed, but worth verifying after extraction. |
| **Could** | `bin/` and `obj/` directories not in `.gitignore` | Build output directories exist locally, but current ignore rules/status need a separate check before changing policy. | Preventive cleanup only; not a standalone extraction blocker if they remain untracked. | Optionally add explicit build-output ignore entries after confirming current ignore behavior. |

## External Dependencies

### nuGet (packages.config)
- `Microsoft.Direct3D.D3D12` 1.618.3 — via project-local `packages\`
- `Microsoft.Direct3D.DXC` 1.8.2505.32 — via project-local `packages\`
- `WinPixEventRuntime` 1.0.240308001 — via project-local `packages\`

Restore command for a fresh standalone checkout:

```powershell
.\Restore-NuGet.ps1
```

The script uses `tools\nuget.exe` when present, otherwise `nuget.exe` from `PATH`. It restores into the project-local `packages/` directory expected by the `.vcxproj`. `nuget.exe` is intentionally not committed; `BUILD.md` documents the prerequisite.

### vcpkg (vcpkg.json)
- `imgui` (with dx12-binding, win32-binding)
- `tinygltf`

### Mixed package managers
The project currently uses **both** NuGet (`packages.config` + project-local `packages\` imports) **and** vcpkg (`vcpkg.json` + `vcpkg_installed/`). This is acceptable for the short-term standalone extraction path as long as NuGet restore targets the project-local `packages/` directory.

## Path Assumptions

| Path In .vcxproj | After Extraction | Status |
|---|---|---|
| `packages\Microsoft.Direct3D.D3D12.1.618.3\` | Restored under project root | OK after restore |
| `packages\Microsoft.Direct3D.DXC.1.8.2505.32\` | Restored under project root | OK after restore |
| `packages\WinPixEventRuntime.1.0.240308001\` | Restored under project root | OK after restore |
| `$(ProjectDir)` | Resolves to new repo root | OK |
| `Shaders\` | Inside project subtree | OK |
| `Assets\` | Inside project subtree | OK |

All shader and asset paths are local (`Shaders/`, `Assets/`). No issues.

## Project And Binary Naming

| Name | Location | Status |
|---|---|---|
| `RtPbrSurvey.vcxproj` | Root project file | Cosmetic — sample-derived name |
| `RtPbrSurvey.exe` | Output binary | Cosmetic — sample-derived name |
| `RtPbrSurvey.lib` | Output library | Cosmetic |
| `RtPbrSurveyApp` | App class (App/RtPbrSurveyApp.h) | Already renamed |
| `RtPbrSurveyEngine` | Engine class (Engine/RtPbrSurveyEngine.h) | Already renamed |
| `Win32Application` | Platform class (Platform/Win32Application.h) | Acceptable — descriptive name |
| `RtPbrSurveyAppClass` | Window class string in Win32Application.cpp:39 | OK — no Sample-derived class name remains |

## Root File Ownership

Files still at the project root that could be moved into subdirectories:

| File | Suggested Owner | Notes |
|---|---|---|
| `Main.cpp` | App/ or root | Entry point; staying at root is acceptable |
| `stdafx.h` | root or Shared/ | Precompiled header; root is fine |
| `stdafx.cpp` | root or Shared/ | PCH creator; root is fine |
| `CubeMesh.h/.cpp` | Scene/ or Engine/ | Mesh utility used by scenes |
| `GltfLoader.h/.cpp` | Scene/ or Engine/ | GLTF loading; maybe Engine/Assets/ |
| `TextureSemantic.h` | Engine/ | Enum for texture slots |
| `WorkMeter.h/.cpp` | Engine/ or Ui/ | Profiling/metering |
| `MyDx12Utils.h` | Platform/Shared/ | DX12 helper utilities |
| `ImGuiWidgets.h` | Ui/ | ImGui widgets |
| `TiniGgltfImpl.cpp` | Scene/ | TinyGLTF implementation file |
| `AGENTS.md` | root or doc/ | Agent instructions for AI |
| `CLAUDE.md` | root or doc/ | Agent instructions for Claude |
| `doc/` | N/A | Planning docs, not needed in standalone repo |

None of these are blockers for extraction; they are cosmetic ownership preferences.

## Assets And Shaders

- `Assets/` — fully inside the project subtree. Contains model files (BoomBox, Avocado, DamagedHelmet). Accessible via `Platform::GetAssetFullPath()`.
- `Shaders/` — fully inside the project subtree. Contains all HLSL shaders and hlsli headers.
- No external references to these directories.

## Generated Files / Ignore Rules

**Current `.gitignore`**:
```
vcpkg_installed/
packages/
imgui.ini
d3d12_debug.log
```

**Missing entries** (should add):
```
bin/
obj/
*.user
*.sdf
*.opensdf
*.suo
ipch/
Debug/
Release/
```

The `bin/` and `obj/` directories contain build outputs. They are not reported by the current inventory status, so this is a preventive cleanup item rather than a blocker.

## Recommended Next Commit-Sized Tasks

1. **Optionally add explicit build-output ignore entries** — Add `bin/`, `obj/`, and Visual Studio user/cache patterns after confirming current ignore behavior.
2. **Remove stale `doc/` planning directory** — All refactoring planning docs are obsolete.
3. **(Optional) Move root utility files** — `CubeMesh.*`, `GltfLoader.*`, `TextureSemantic.h`, `WorkMeter.*`, `MyDx12Utils.h`, `ImGuiWidgets.h`, `TiniGgltfImpl.cpp` into subdirectories.

## Verification

- `git diff --check`: clean (no whitespace errors).
- The inventory itself was produced as an untracked documentation file before review/commit.
- `.vcxproj` now imports NuGet props/targets from project-local `packages\...`.
- `Restore-NuGet.ps1` documents and automates project-local NuGet restore.
- `BUILD.md` documents the decision to keep `nuget.exe` external while allowing an ignored local `tools\nuget.exe`.
- Current checkout uses an untracked `packages` junction to the existing parent `..\packages` folder for compatibility.
- Debug x64 build succeeds with project-local package paths through the junction.

---

*Generated by opencode on 2026-07-05 per `opencode-standalone-readiness-inventory-task.md`.*
