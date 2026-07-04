# OpenCode Task: Audit DXSampleHelper Shim After Include Removal

## Objective

Audit the state after `DXSampleHelper.h` include removal and propose the next smallest safe refactor step.

The goal is **not** to delete useful DX12 helper utilities. DX12/RHI code may keep using DX12-oriented helpers. The goal is to decide what remains of `DXSampleHelper.h` as a compatibility shim and prevent Sample-named dependencies from leaking into new ownership boundaries.

## Current Context

Workspace:

```txt
C:\work\DirectX-Graphics-Samples\Samples\Desktop\D3D12HelloWorld\src\HelloTextureModified
```

Policy document:

```txt
doc\branch\refactor\engine-separation.md
```

Previous task document:

```txt
doc\branch\refactor\opencode-dxsamplehelper-rhi-ownership-task.md
```

Expected current direction:

* Renderer headers and implementation files should no longer include `DXSampleHelper.h`.
* Non-renderer includes may already have been removed by previous OpenCode work.
* `DXSampleHelper.h` may remain as a compatibility aggregation header.
* DX12 helper functionality should remain available through explicit owner headers such as `Shared/Error.h`, `Shared/D3d12Helpers.h`, `Shared/D3d12DebugName.h`, `Platform/FileIO.h`, and `Platform/ShaderCompiler.h`.

## First Checks

Run these first and report the exact output:

```powershell
rg -n "DXSampleHelper\.h" -g "*.h" -g "*.cpp"
```

```powershell
Get-Content -LiteralPath DXSampleHelper.h
```

```powershell
rg -n "\bComPtr<|using Microsoft::WRL::ComPtr|ThrowIfFailed|HrException|HrToString|ReadDataFromFile|CompileShaderFromFile|NAME_D3D12_OBJECT|CalculateConstantBufferByteSize" -g "*.h" -g "*.cpp"
```

## Ownership Rule

Use this rule when interpreting the results:

### Allowed

DX12/RHI or renderer implementation code may depend on explicit DX12 helper owner headers:

```cpp
#include "Shared/Error.h"
#include "Shared/D3d12Helpers.h"
#include "Shared/D3d12DebugName.h"
```

Platform-facing code may depend on explicit platform owner headers:

```cpp
#include "Platform/FileIO.h"
#include "Platform/ShaderCompiler.h"
```

### Avoid

Do not add new includes of:

```cpp
#include "DXSampleHelper.h"
#include "../DXSampleHelper.h"
```

unless the file is explicitly a temporary compatibility bridge.

### Do Not Treat As A Goal

Do not treat these as goals by themselves:

* Removing every DX12 helper.
* Hiding all D3D12 usage from renderer/DX12 code.
* Deleting `DXSampleHelper.h` before all compatibility users are understood.

## Suggested Work Units

Choose the smallest work unit that matches the current checkout.

### Work Unit A: Remaining Include Replacement

Use this if `rg -n "DXSampleHelper\.h"` still reports matches.

Scope:

* Replace remaining `DXSampleHelper.h` includes with direct owner headers.
* Prefer starting with lower-level files such as:
  * `WorkMeter.h`
  * `GraphicsDevice.h`
  * `MyDx12Utils.h`
* Only touch `DXSample.h`, `D3D12HelloTexture.h`, or `D3D12HelloTexture.cpp` if the include replacement is straightforward.

Out of scope:

* Splitting `DXSample`.
* Moving files into new directories.
* App/UI extraction.

### Work Unit B: ComPtr Shim Audit

Use this if there are no remaining `DXSampleHelper.h` includes, but `DXSampleHelper.h` still exposes:

```cpp
using Microsoft::WRL::ComPtr;
```

Scope:

* Identify files that still rely on bare `ComPtr<T>`.
* Classify each file by owner:
  * App/sample shell
  * Platform
  * DX12/RHI
  * Renderer
  * Shared
* Convert a small low-risk set to `Microsoft::WRL::ComPtr<T>` if the owner is clear.
* If conversion is too broad, produce only an inventory and recommendation.

Out of scope:

* Mechanical conversion across the whole repository in one patch.
* Deleting the compatibility shim.

### Work Unit C: DXSampleHelper Shim Decision Note

Use this if no safe code change is obvious.

Scope:

* Update `doc\branch\refactor\engine-separation.md` with a short policy note:
  * `DXSampleHelper.h` is a temporary compatibility shim.
  * DX12 helpers are allowed in DX12/RHI code through explicit owner headers.
  * New code should include owner headers directly.
  * Deletion or relocation of the shim should happen only after all compatibility users are gone.

Out of scope:

* Any behavior change.

## Implementation Guidance

For code changes:

1. Keep the patch small and reviewable.
2. Do not reorder unrelated includes.
3. Prefer explicit owner headers over aggregate headers.
4. Prefer `Microsoft::WRL::ComPtr<T>` in headers.
5. `.cpp` files may use a local `using Microsoft::WRL::ComPtr;` only when that keeps a narrow implementation file readable.
6. Preserve behavior.

## Documentation Requirement

If code changes are made, update:

```txt
doc\branch\refactor\engine-separation.md
```

Add a commit-unit note with:

* What ownership boundary was clarified.
* Which files were changed.
* Which owner headers replaced `DXSampleHelper.h`, if any.
* Whether `DXSampleHelper.h` remains and why.
* Verification results.

## Verification

Run:

```powershell
git diff --check
```

Run Debug x64 build if code changed:

```powershell
& "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\amd64\MSBuild.exe" D3D12HelloTextureModified.vcxproj /p:Configuration=Debug /p:Platform=x64 /m
```

Also report:

```powershell
rg -n "DXSampleHelper\.h" -g "*.h" -g "*.cpp"
```

## Expected Report

Write a concise report with:

* Which work unit was chosen: A, B, or C.
* Files changed, if any.
* Remaining `DXSampleHelper.h` includes, if any.
* Remaining purpose of `DXSampleHelper.h`.
* `ComPtr` shim recommendation.
* `git diff --check` result.
* Build result, if run.
* Final status: `done` or `blocked`.

## Hard Rules

Do not commit, push, merge, reset, checkout, or switch branches.

