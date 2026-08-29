# Hybrid Reflection Denoiser Contracts Work Log

## 2026-08-29: Contract extraction started

- Started `features/hybrid-reflection-denoiser-contracts` from `main` at `16c8ca6`.
- Audited the implemented temporal resources, ping-pong ownership, reset behavior, depth/normal rejection, moments, persistent confidence, and the bounded surface-filter experiment.
- Extracted a focused denoiser contract that separates implemented temporal behavior from the future edge-aware spatial-pass boundary.
- Fixed `ReflectionDenoisedRadiance` as a future distinct, stateless spatial output. The first spatial implementation must not overwrite or feed back into temporal history.
- Kept the unweighted radiance boundary and final `LightPass` ownership unchanged.
- Corrected a stale resource-table statement: `ReflectionResolvedRadiance.a` carries the implemented temporal-validity diagnostic code rather than a constant `1`. RGB semantics are unchanged.
- Validation was limited to contract-to-code inspection, Markdown link/whitespace checks, and English/Japanese work-log parity. No code or shader changed, so Debug x64 and runtime validation were not repeated.

Status: done
