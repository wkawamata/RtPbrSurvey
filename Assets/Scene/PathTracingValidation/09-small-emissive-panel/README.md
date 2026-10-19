# Small Emissive Panel

A 0.3m square emitter at height 3m above the receiver. Compare BSDF/NEE/MIS
in the central floor ROI. Independent area quadrature supplies the reference.
Small-light BSDF noise is expected; brightness must not change between estimators.

Reproduce with validate_emissive_extended.py --cases small large --samples 256.
Raw captures and logs remain under bin/PathTracingValidation.
