# D3D12 Debug Layer Check

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

Generated logs such as `d3d12_debug.log` are not committed. See Git Hygiene in [AGENTS.md](../AGENTS.md).
