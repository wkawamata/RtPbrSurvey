# OpenCode Task: Re-own DXSampleHelper as DX12/RHI Utilities

## Objective

Clarify and implement the next small refactor step for `DXSampleHelper.h`.

The goal is **not** to delete all DX12 helper utilities. DX12/RHI code may keep using useful DX12 helpers. The goal is to stop treating `DXSampleHelper.h` as a DirectX-Graphics-Samples owned catch-all header and move toward explicit owner headers.

In other words:

* Do not pursue "remove every helper" as the objective.
* Do pursue "make ownership explicit".
* Keep DX12-specific helpers available to the DX12/RHI layer.
* Avoid spreading Sample-named compatibility includes into new code.

## Current Context

Workspace:

```txt
C:\work\DirectX-Graphics-Samples\Samples\Desktop\D3D12HelloWorld\src\RtPbrSurvey
```

Policy document:

```txt
doc\branch\refactor\engine-separation.md
```

`DXSampleHelper.h` has already been reduced to a compatibility aggregation header. It currently includes owner headers such as:

```cpp
#include "Platform/FileIO.h"
#include "Shared/Error.h"
#include "Shared/D3d12DebugName.h"
#include "Shared/D3d12Helpers.h"
#include "Platform/ShaderCompiler.h"
```

It may also still expose:

```cpp
using Microsoft::WRL::ComPtr;
```

That compatibility shim can remain for now if needed.

## Ownership Rule

Use this ownership model:

### Allowed

DX12/RHI implementation code may include DX12 helper headers directly:

```cpp
#include "Shared/Error.h"
#include "Shared/D3d12DebugName.h"
#include "Shared/D3d12Helpers.h"
```

This is acceptable when the file already belongs to renderer/DX12 implementation code and uses D3D12 concepts directly.

### Avoid

Do not add new includes of:

```cpp
#include "DXSampleHelper.h"
#include "../DXSampleHelper.h"
```

unless the file is explicitly a temporary compatibility bridge.

### Prefer

Replace `DXSampleHelper.h` includes with the smallest direct owner header set needed by the file:

* HRESULT / `ThrowIfFailed` / error types:
  * `Shared/Error.h`
* D3D12 object naming:
  * `Shared/D3d12DebugName.h`
* D3D12 alignment or utility helpers:
  * `Shared/D3d12Helpers.h`
* File loading:
  * `Platform/FileIO.h`
* Shader compilation:
  * `Platform/ShaderCompiler.h`
* WRL smart pointers:
  * `<wrl/client.h>` and `Microsoft::WRL::ComPtr<T>`

## Suggested First Work Unit

Replace remaining implementation-side `DXSampleHelper.h` includes in `Renderer/*.cpp` with direct owner headers.

Start with files reported by:

```powershell
rg -n "DXSampleHelper\.h" Renderer -g "*.cpp" -g "*.h"
```

Expected examples may include:

```txt
Renderer\AccelerationStructureResources.cpp
Renderer\BrdfLut.cpp
Renderer\DebugDumpCapture.cpp
Renderer\EnvironmentMap.cpp
Renderer\GBuffer.cpp
Renderer\HdrOutput.cpp
Renderer\MaterialBuffer.cpp
Renderer\RootSignatureFactory.cpp
Renderer\StagedDescriptorAllocator_Test.cpp
```

Do not blindly replace all of them in one edit if the required owner headers differ. Keep the patch small and reviewable.

## Implementation Guidance

For each touched file:

1. Remove `DXSampleHelper.h`.
2. Add only the direct headers required by symbols used in that file.
3. If the file relies on bare `ComPtr<T>`, either:
   * convert to `Microsoft::WRL::ComPtr<T>`, or
   * add a local using declaration only inside `.cpp` if that is consistent with nearby code.
4. Do not reorder unrelated includes.
5. Keep behavior unchanged.
6. Do not move helper implementations unless necessary for this small unit.

## Out Of Scope

Do not do these in this task:

* Delete `DXSampleHelper.h`.
* Rename directories.
* Move files into a new `Engine/Rhi/Dx12` directory.
* Change public renderer APIs unless required to remove the include.
* Refactor `DXSample`, `Win32Application`, or app framework ownership.
* Commit, push, merge, reset, checkout, or switch branches.

## Documentation Requirement

Update:

```txt
doc\branch\refactor\engine-separation.md
```

Add a short commit-unit note explaining:

* that DX12 helpers are allowed in the DX12/RHI layer,
* that `DXSampleHelper.h` is being treated as a temporary compatibility shim,
* which direct owner headers were introduced,
* what verification was run.

## Verification

Run:

```powershell
git diff --check
```

Run Debug x64 build:

```powershell
& "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\amd64\MSBuild.exe" RtPbrSurvey.vcxproj /p:Configuration=Debug /p:Platform=x64 /m
```

If available, also confirm:

```powershell
rg -n "DXSampleHelper\.h" Renderer -g "*.cpp" -g "*.h"
```

Report whether matches remain and why.

## Expected Report

Write a concise report with:

* Files changed.
* Direct owner headers added.
* Remaining `DXSampleHelper.h` includes, if any.
* Build result.
* `git diff --check` result.
* Final status: `done` or `blocked`.

