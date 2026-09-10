# Build Notes

## Prerequisites

- Visual Studio 2022 with the Desktop development with C++ workload.
- Windows 10 SDK or newer.
- vcpkg available to MSBuild. Set `VCPKG_ROOT` or pass `/p:VcpkgRoot=<path>\` when building.
- `nuget.exe` available on `PATH`, or a local untracked copy at `tools\nuget.exe`.

`nuget.exe` is intentionally not committed. This keeps the repository free of tool binaries while making the dependency on the external tool explicit.

## Restore NuGet Packages

Restore packages into the project-local `packages/` directory:

```powershell
.\Restore-NuGet.ps1
```

If `nuget.exe` is not on `PATH`, pass it explicitly:

```powershell
.\Restore-NuGet.ps1 -NuGetExe C:\path\to\nuget.exe
```

Or place a local copy at `tools\nuget.exe`. That file is ignored by Git.

## Build

Build the standalone solution:

```powershell
msbuild RtPbrSurvey.sln /p:Configuration=Debug /p:Platform=x64
```

If vcpkg is not globally integrated, pass the root explicitly:

```powershell
msbuild RtPbrSurvey.sln /p:Configuration=Debug /p:Platform=x64 /p:VcpkgRoot=C:\dev\vcpkg\
```

## Optional NVIDIA DLSS Super Resolution

DLSS SR is integrated through NVIDIA Streamline and remains optional. Extract
the supported SDK into `third_party\streamline-sdk-2.12.0`, then use the normal
build command. The build detects the SDK, enables the Streamline adapter, and
copies the required runtime DLLs beside the executable.

See [doc/third-party/streamline-sdk.md](doc/third-party/streamline-sdk.md) for
the official download, expected archive hash, alternate SDK path properties,
runtime use, and validation procedure.
