# Deep Emissive Transport and RR

An enclosed diffuse room is illuminated only by a downward-facing emissive panel.
Compare BSDF/NEE/MIS at eight path segments with RR OFF/ON, then compare two/eight
segments in MIS to demonstrate significant deep-path energy.

Default: MIS, eight segments, RR ON. No environment or analytic lights.
This is relative mean preservation, not an independent absolute transport oracle.

Reproduce with Tests/PathTracing/validate_emissive_rr.py --samples 128.
Raw HDR captures and logs remain under bin/PathTracingValidation.
