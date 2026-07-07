# Standalone Repository Readiness Inventory

## Summary

The project is close to being extractable from the `DirectX-Graphics-Samples` repository. The major refactoring (DXSample removal, SampleApp→HelloTextureApp rename, file moves into App/Platform/Engine/, RenderPass→FrameGraph) is complete. The remaining blockers are all **nuGet package path assumptions** in `.vcxproj` and two cosmetic name remnants.

## Current Good State

- **No `DXSample` references** in any source file (0 matches).
- **No `SampleApp` references** in any source file (0 matches).
- **No `D3D12HelloTexture` references** in any source file (0 matches).
- **All files moved** into App/, Platform/, Engine/, Renderer/, Scene/, Ui/, Shared/ subdirectories.
- **`Platform::IApplication`** host interface decouples Win32Application from the app class.
- **No dead planning docs** interfere with the build; `doc/` is not referenced by `.vcxproj`.
- **`git diff --check`**: clean (no whitespace errors).
- **`.gitignore`** exists and covers `vcpkg_installed/`, `imgui.ini`, `d3d12_debug.log`.

## Blocking Issues

| Priority | Issue | Evidence | Impact | Recommended Task |
|---|---|---|---|---|
| **Must** | nuGet packages referenced via `..\packages\` relative path | `.vcxproj` lines 3-4, 450-462: `..\packages\Microsoft.Direct3D.D3D12.1.618.3\` etc. | After `git filter-repo`, the `..\packages\` parent path does not exist. **Build breaks.** | Move `packages/` into the project subtree or switch to vcpkg for D3D12/DXC/WinPixEventRuntime. |
| **Must** | `packages.config` references packages that are siblings, not children | `packages.config` exists alongside vcpkg.json (dual package management) | After extraction, the `..\packages\` path is lost. | Resolve package strategy: either keep nuGet with local `packages/` dir, or fully migrate to vcpkg for D3D12/DXC. |
| **Should** | `$(ProjectDir)` added to AdditionalIncludeDirectories | `.vcxproj:76,108` — works but is a workaround for subdirectory source files | After extraction, `$(ProjectDir)` resolves to the new repo root, which is fine. | No change needed, but worth verifying after extraction. |
| **Could** | `bin/` and `obj/` directories not in `.gitignore` | Build output directories exist locally, but current ignore rules/status need a separate check before changing policy. | Preventive cleanup only; not a standalone extraction blocker if they remain untracked. | Optionally add explicit build-output ignore entries after confirming current ignore behavior. |

## External Dependencies

### nuGet (packages.config)
- `Microsoft.Direct3D.D3D12` 1.618.3 — via `..\packages\`
- `Microsoft.Direct3D.DXC` 1.8.2505.32 — via `..\packages\`
- `WinPixEventRuntime` 1.0.240308001 — via `..\packages\`

### vcpkg (vcpkg.json)
- `imgui` (with dx12-binding, win32-binding)
- `tinygltf`

### Mixed package managers
The project currently uses **both** nuGet (`packages.config` + `..\packages\` imports) **and** vcpkg (`vcpkg.json` + `vcpkg_installed/`). This duality must be resolved before extraction.

## Path Assumptions

| Path In .vcxproj | After Extraction | Status |
|---|---|---|
| `..\packages\Microsoft.Direct3D.D3D12.1.618.3\` | Parent no longer has `packages/` | **BREAKS** |
| `..\packages\Microsoft.Direct3D.DXC.1.8.2505.32\` | Same | **BREAKS** |
| `..\packages\WinPixEventRuntime.1.0.240308001\` | Same | **BREAKS** |
| `$(ProjectDir)` | Resolves to new repo root | OK |
| `Shaders\` | Inside project subtree | OK |
| `Assets\` | Inside project subtree | OK |

All shader and asset paths are local (`Shaders/`, `Assets/`). No issues.

## Project And Binary Naming

| Name | Location | Status |
|---|---|---|
| `D3D12HelloTextureModified.vcxproj` | Root project file | Cosmetic — sample-derived name |
| `D3D12HelloTextureModified.exe` | Output binary | Cosmetic — sample-derived name |
| `D3D12HelloTextureModified.lib` | Output library | Cosmetic |
| `HelloTextureApp` | App class (App/HelloTextureApp.h) | Already renamed |
| `HelloTextureEngine` | Engine class (Engine/HelloTextureEngine.h) | Already renamed |
| `Win32Application` | Platform class (Platform/Win32Application.h) | Acceptable — descriptive name |
| `HelloTextureAppClass` | Window class string in Win32Application.cpp:39 | OK — no Sample-derived class name remains |

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

**Current `.gitignore`** (3 entries):
```
vcpkg_installed/
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

1. **Fix nuGet package paths** — Move `packages/` into the project tree (e.g., as a sibling or use vcpkg for D3D12/DXC). This is the only *must-fix-before-extraction* issue.
2. **Optionally add explicit build-output ignore entries** — Add `bin/`, `obj/`, and Visual Studio user/cache patterns after confirming current ignore behavior.
3. **Remove stale `doc/` planning directory** — All refactoring planning docs are obsolete.
4. **(Optional) Move root utility files** — `CubeMesh.*`, `GltfLoader.*`, `TextureSemantic.h`, `WorkMeter.*`, `MyDx12Utils.h`, `ImGuiWidgets.h`, `TiniGgltfImpl.cpp` into subdirectories.

## Verification

- `git diff --check`: clean (no whitespace errors).
- The inventory itself was produced as an untracked documentation file before review/commit.
- No code changes were made in this inventory task.
- Build skipped (inventory-only task per task scope).

---

*Generated by opencode on 2026-07-05 per `opencode-standalone-readiness-inventory-task.md`.*
