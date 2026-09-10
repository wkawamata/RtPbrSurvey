# DLSS Feature Recreation Fix Work Log

## 2026-08-29: Unsupported quality-mode audit

- Reproduced the reported host path from the UI sequence: enable DLSS, select DLAA, then select Ultra Quality.
- The Streamline log repeatedly reported NGX feature creation failure `0xbad00010` before an unhandled exception.
- Audited the host mode mapping, optimal-settings query, render-size recreation, and per-frame `slDLSSSetOptions`/`slEvaluateFeature` path.
- Removed Ultra Quality from the application quality-mode contract and UI. The public NVIDIA Streamline sample exposes DLAA, Quality, Balanced, Performance, and Ultra Performance; the host must not expose an SDK enum that the active NGX runtime rejects as an unsupported parameter.
- This hotfix is independent of Hybrid Reflection denoiser contracts.
- Debug x64 build succeeded with zero errors and one known duplicate-vcpkg-import warning.
- Runtime acceptance passed: Ultra Quality is absent, and the DLAA, Quality, and Balanced selection path completed without the reported NGX feature-creation failure or exception.

Status: done
