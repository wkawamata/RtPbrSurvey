# RR automated evaluation (2026-11-01)

User subjective evaluation is on hold. Full-scene rotation was previously accepted;
full-scene translation quality is pending. Automated success is not visual acceptance.

## Changes

Object-motion validation now captures both RR output slots and selects the verified
published slot from packet metadata. It checks finite, nonconstant output, finite
position/motion, nonzero surface motion, fixed-camera transforms, pause/resume and
uninterrupted RR publication. Full scenes retain floor, walls, bars and reflection
sphere. Single-helmet fixtures additionally compare every visible surface's motion
against object/camera reprojection. Tests use the same immutable packet for all captures.

A new fixed-background check found 4.23e-6 UV drift in the full scene and 5.39e-6
in the single-object fixture, exceeding the existing 2e-6 threshold. Camera matrices
were exactly identical. The conversion now supplies mathematically zero background
motion when unjittered camera matrices are bitwise identical, avoiding inverse-ray
roundoff. Moving cameras retain the existing reprojection path and admission checks.
The threshold was not increased. Root constants grow from 38 to 39 DWORDs; no extra
GPU resource, dispatch or CPU wait is added.

## Results

- Normal VS Debug and NRD/RR CMake evaluation builds succeeded.
- CPU CTest: 14/14 passed.
- Full-scene rotation and translation: five checkpoints each passed. Reset count 1,
  cancelled 0, finite verified RR output. Fixed-background motion is now exactly 0.
- Single-object rotation and translation: five checkpoints each passed. Maximum UV
  reprojection errors are 1.93070e-6 and 3.81582e-6, respectively. Background motion 0.
- Static RR regression: 10/10 passed before the background change; targeted RR output,
  background and jitter regression: 3/3 passed after it.
- Original camera/backend/sampling/projection/resize lifecycle: 19/19 passed after
  the change, with 4 spp, frame jitter, 4 bounces and Accumulate ON. Camera motion
  errors remained within the existing thresholds. No captured D3D12 ERROR/CORRUPTION.

Evidence stays local under build/reconstruction-input-validation/user-test-rr/:
- auto-full-motion-v2-20261101 and auto-single-motion-v2-20261101 preserve pre-fix findings.
- auto-static-regression-20261101: original 10-case regression.
- auto-final-full-20261101 and auto-final-single-20261101: post-fix moving-object results.
- auto-final-background-20261101: post-fix targeted static regression.
- auto-final-camera-lifecycle-20261101: post-fix 19-checkpoint regression.

## Remaining scope

Subjective full-scene translation, reflected moving-object detail and disocclusion
quality remain pending. These tests do not establish support for animated emissive,
alpha-masked or transmission/volume objects. Long-duration stability and Release
performance evaluation are separate tasks. Normal VS NRD support is not inferred
from the NRD-enabled CMake lifecycle run.
