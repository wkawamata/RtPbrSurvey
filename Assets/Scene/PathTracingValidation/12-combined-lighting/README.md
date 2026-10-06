# Combined Emissive, Point and Environment Lighting

A warm emissive rectangle, cool point light and constant-white environment light
illuminate the same receiver. Compare linear HDR RGB means, not tone-mapped sums.
Default: all sources ON, emissive MIS, constant-environment MIS, two segments.

Validation compares the sum of isolated sources with combined output per seed,
then compares BSDF/NEE/MIS and joint MIS vs pure BSDF environment sampling.

Reproduce with Tests/PathTracing/validate_emissive_combined.py --samples 128.
Raw HDR captures and logs remain under bin/PathTracingValidation.
