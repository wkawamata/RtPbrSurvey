# Hybrid Reflection Dynamic Temporal Diagnostics Report

## Status

Status: **PASS WITH LIMITATION / non-perceptual diagnostics complete**

This phase makes controlled camera-motion, reversal, rejection, and post-stop settling behavior explainable in the linear-HDR diagnostic domain. It does not treat the earlier subjective object-rotation delay as a confirmed defect.

HDR diagnostic schema 12 records the deterministic forward/reverse/stop timeline, stored motion-vector magnitude, hit distance, exhaustive temporal status rates, and T50/T90/T95 settling of resolved-radiance ROI mean luminance. `Tests/HybridReflection/Test-DynamicTemporalReport.ps1` independently validates the report contract and recomputes the settling metrics.

The controlled 30-degree camera orbit used three validated metallic-sphere ROIs, stochastic sampling, history weight 0.9, 12 frames per direction, and 32 warm-up frames.

| Roughness | Moving acceptance | Depth reject | Normal reject | Stationary acceptance | T50 | T90 | T95 |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1.0 | 0.9572 | 0.0424 | 0.0004 | 1.0000 | 6 | 11 | 13 |
| 0.35 | 0.9407 | 0.0590 | 0.0003 | 1.0000 | 6 | 19 | 20 |
| 0.0 | 0.9494 | 0.0439 | 0.0067 | 1.0000 | 6 | 15 | 17 |

All cases returned to zero motion and full history acceptance after stop. The measured T90 range was 11 to 19 frames and T95 was 13 to 20 frames. This controlled camera-motion result does not show an excessive long-duration history tail.

All regenerated schema 12 reports passed the independent validator. Debug x64/HLSL build completed with zero errors. D3D12 Debug Layer reported zero errors and only the three known buffer initial-state warnings per run.

This result does not establish object-motion validity, reflection-hit identity validation, Lit perceptual acceptability, scene generalization, or production denoiser readiness. This diagnostic branch closes with implementation, automated measurements, artifact validation, build, and Debug Layer audit complete. A live Lit review is transferred to a later, independent evaluation branch. Object-motion/hit-identity work is promoted into a new implementation unit only if that review exposes a practical artifact; it is not unfinished scope in this branch.
