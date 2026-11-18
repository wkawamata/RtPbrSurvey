# VSync OFF presentation path and GUI location

2026-11-18. The checkbox now appears first in Basic Settings; WorkMeter retains presentation diagnostics and includes actual LastPresentInterval/Flags and tearing support in OutputAverage metadata.

The earlier path used flip discard, swap-chain flags 0 and Present(syncInterval, 0). It changed syncInterval to zero for OFF, but did not opt into tearing. This was a missing presentation capability, not proof of the user's exact 30 FPS cause. The visible-window issue must still be rechecked after restarting the rebuilt application.

The corrected path queries IDXGIFactory5::CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING). On support, the swap chain is created with DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING, and that flag is preserved by ResizeBuffers. Present adds DXGI_PRESENT_ALLOW_TEARING only when syncInterval=0 and GetFullscreenState confirms windowed mode. Unsupported or exclusive-fullscreen paths do not receive this flag; VSync ON remains interval=1, flags=0. This follows Microsoft's documented requirements: https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/variable-refresh-rate-displays

Validation: Release/Debug builds and existing CPU timing/EvaluationState tests pass. Debug zero-instance Hybrid runs exit normally with D3D12 ERROR 0 for both settings: OFF interval=0 flags=512 supported=true; ON interval=1 flags=0 supported=true. Full interactive GUI/visible-window pacing and live resize were not exercised in this step. Automated hidden-window timing does not prove that visible presentation throttling is fixed.

Debug evidence: bin/Benchmark/tearing-validation/. Release OFF zero-instance five-run output: bin/Benchmark/tearing-off-zero-5/.