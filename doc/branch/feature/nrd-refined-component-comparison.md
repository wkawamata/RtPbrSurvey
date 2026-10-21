# Refined Component Output Comparison

## Scope and Evidence

This is a separate Step 2 study using the completed 32768-sample component
reference cohort. The frozen 8192-sample comparison is not rewritten.
Runner: `Tests/PathTracing/compare_nrd_refined_components.py`.

Report: `bin/PathTracingValidation/nrd-refined-comparison-20261021/report.json`.
SHA-256: `0ea66ec175d1b74edcd3d232fa15b8cd9f2a46d02614e8752cd470448ec76402`.
Status: `completed-refined-component-comparison`; quality: `not-promoted`.

This run revalidates six refined references (four previously acquired component
captures and two historical Total captures), the earlier reference/bridge
chains, and 49 historical seed-7 output captures. Fresh GPU runs in this study:
zero. Inputs are fixed by the reviewed report hash, exact case inventories,
artifact/raw/log hashes, commands, fixtures, metadata and runtime state.
Reference qualifications are recomputed from raw buffers. Material restoration,
GPU composition, admission and the historical profile-change attribution are
rechecked using unchanged helpers.

Transport, camera, masks and settings remain unchanged: one spp, pixel-center,
four bounces, environment mode 7. Output checkpoints are 30/120 elapsed frames,
not 30/120 samples of unlimited NRD history; the history cap remains 30.
Profiles remain control (Diffuse/Specular pre-pass radii 30/50) and diffuse-5
(5/50). No renderer, shader, SDK setting or default is modified.

## Inventory

There are 168 component result rows (two profiles, two checkpoints, three
components, 14 ROIs):

- 136 active assessments have qualified references; 133 meet the 5% mean-bias
  target and three do not.
- 28 pure-metal Diffuse assessments verify exact-zero restored contribution.
- Four Diffuse mirror-proxy assessments are excluded because their shared
  reference pair remains unqualified at 5.0129% NRMSE.
- 52 joint descriptions have qualified Total and both component references;
  four joint descriptions at the focus ROI remain excluded. There is no
  cancellation or joint contribution verdict for that ROI.

Mean RGB bias is the same project diagnostic as before:
`length(mean(output-reference))/length(mean(reference))`, target 5%.
Reference qualification still requires pair NRMSE no greater than 3%.
These are point estimates, not confidence intervals or SDK guarantees.

## Focus ROI

`dielectric-smooth/mirror-proxy`, unchanged 792-pixel mask:

| Profile | Frames | Total mean RGB bias | Specular mean RGB bias | Diffuse |
| --- | ---: | ---: | ---: | --- |
| control | 30 | 8.3054% (outside target) | 2.9570% (within target) | Excluded |
| control | 120 | 5.4107% (outside target) | 2.2717% (within target) | Excluded |
| diffuse-5 | 30 | 5.7605% (outside target) | 2.9570% (within target) | Excluded |
| diffuse-5 | 120 | 2.6491% (within target) | 2.2717% (within target) | Excluded |

All Total mean-error channels are negative at these checkpoints. The three
outside-target rows are exactly the Total rows identified above; all other
qualified component/ROI rows meet this mean-bias target.

The Specular reference pair NRMSE is 2.5636%, and Total is 2.6533%. Diffuse is
still 5.0129%, so no absolute Diffuse component bias or full-lobe cancellation
result is published for this ROI. In particular, normalized Total and Specular
percentages use different reference denominators and must not be subtracted
as if they were additive contributions.

The prior attribution established that the profile's output change in the
focus ROI is in the restored Diffuse contribution, with unchanged Specular.
The new result additionally shows that Specular's *mean-color* bias is within
the project target. This narrows the observations, but does not prove that all
Specular behavior is correct, identify a Diffuse integration defect, or assign
the entire Total bias to one cause.

Specular output NRMSE is approximately 26.64% at frame 30 and 26.55% at frame
120 against the pair-mean reference, despite its much smaller mean-color bias.
Those numbers are descriptive pixelwise error, not a passed spatial-detail,
noise, blur or Native-relative quality test. Accumulated Native Diffuse/Specular
RMSE comparisons are not available in this cohort; separate Total-versus-Native
assessments are not repeated here. Likewise, diffuse-5/frame 120
meeting the mean-bias target is not a complete quality pass or a reason to
promote that profile.

## Validation and Next Work

Python tests: 190/190. Five new tests cover the refined report's policy,
partial-vs-complete verdict, duplicate/missing cases, wrong acquisition roles,
missing numerical evidence, report identity and independent assessment of
qualified Total/Specular while Diffuse remains excluded. Revalidated capture
logs contain no D3D12 errors. No new temporal or human review was performed.

Next priority is the one remaining Diffuse reference uncertainty. Validate a
faster reference acquisition method before substantially increasing the sample
budget; preserve the existing camera, masks, estimator and acceptance criteria.
Do not substitute the rejected 16-spp path or silently relax the 3% criterion.
The subsequent `nrd-fast-reference-pilot.md` study confirmed bit identity for
Debug Layer opt-out but achieved only 1.044x median speedup, below its 1.5x
target. It is not adopted as the high-sample acquisition strategy.
After reference qualification, re-evaluate Diffuse and joint contributions,
then confirm any proposed setting change across seeds and temporal cases.
The default-off policy and existing pre-pass defaults remain unchanged.
