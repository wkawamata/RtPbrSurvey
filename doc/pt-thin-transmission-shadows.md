# Smooth thin transmission shadows

## Scope

Implemented on `codex/pt-thin-transmission-shadows`, from origin/main. This is the first part of roadmap item 3 (glass shadows), corresponding to stages 0–1 of the scene roadmap.

Analytic directional, point and spot direct-light visibility now supports smooth thin transmission. Per interface, RGB throughput is:

`transmission * (1 - metallic) * (1 - Fresnel(abs(cosine), IOR)) * tint`

This matches the expected transmission weight of the existing camera thin model. Distinct surfaces remain distinct: the thin box has two interfaces; a hollow tumbler can cross four. Instance deduplication would incorrectly omit real walls. The original WarmGlassTable materials are unchanged. Saved Scene `PT Warm Glass Thin Shadows` supplies smooth tumblers and a cyan smooth thin panel, with the solid Volume slab as an opaque-shadow control.

Non-delta rough thin transmission remains conservatively opaque (IOR 1 remains delta). Volume shadow refraction/absorption, environment and emissive NEE visibility, and caustics are not implemented here. A lighter thin shadow does not demonstrate focused caustics or a correct thick-glass shadow.

## Traversal and cost

Traversal repeatedly resolves the nearest accepted material hit, respects alpha cutouts, evaluates sampled material textures, and advances the original ray TMin by one representable float. Sixteen thin interfaces are supported; a final query distinguishes a miss from an over-budget surface. Opaque/unsupported blockers and invalid data terminate conservatively.

Thin Shadow Status exposes R=status bits (1 budget, 2 unsupported, 4 invalid), G=maximum interfaces, B=query count along the camera path. Read raw SceneColor at one sample/frame with Accumulate OFF; otherwise averaging changes these values.

A shared HLSL source compiles into separate ordinary and thin-shadow PSOs. Thin-material scenes or this diagnostic select the latter. An initial unspecialized version increased the opaque test GPU time by 5.9% and was replaced. The final opaque kernel code hash equals the original main kernel (`a607fbad4fffe5495713e77e3fcd9e12`).

## Validation, 2026-11-14

- Debug, Release and CMake evaluation Debug builds passed. VS builds retain one existing MSB4011 warning.
- SceneRendererSettingsTests passed, including diagnostic enum persistence.
- `Tests/PathTracing/validate_thin_shadows.py`: 23 GPU captures / 21 comparisons passed. Covers Fresnel, tint, metallic/transmission mixture, reversed side, layered sheets, closed box, opaque blockers, alpha cutout, textures, finite point-light segments and exact/overflow budgets. Worst absolute RGB error was about 0.000171, within the declared tolerance.
- Accepted automated logs contained no ERROR/CORRUPTION. This is not a claim that Release captures enabled the D3D12 debug layer.
- Opaque A/B: six isolated Release runs, 1280x720, Native 1 spp / 4 bounces; mean observed GPU time 4.1361 ms before, 4.1313 ms after. The -0.12% difference is measurement noise, not a speedup claim. All six output pixel arrays were identical. This measures one opaque workload, not the added cost in thin scenes.
- Native scene comparison: same camera/seed, 640x360, 256 accumulated samples. The tumbler footprints become lighter and preserve the wood; the cyan panel transmits tinted light. The solid Volume slab still blocks analytic direct shadows.
- RR smoke with automated camera orbit: verified RR output, 36 completed / 35 continuous, 1 reset, zero cancelled and zero input rejected. This checks execution, not subjective motion quality.

Local evidence lives under `build/thin-shadow-20261114/`: `analytic-split/report.json`, `opaque-ab-v2/report.json`, and `review/report.json` plus before/after/orbit PNGs. Generated evidence is excluded from Git. Earlier failed fixture and unspecialized performance runs are not acceptance results.

## Next scope

Implement thick planar Volume shadow connection with refraction and distance absorption, then hollow boundaries and caustics as separate changes. The user visually accepted the thin-shadow scene on 2026-11-14.
