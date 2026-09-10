#!/usr/bin/env python3

import argparse
import json
import math
from pathlib import Path


def smith_g1_ggx(ndot_direction: float, alpha: float) -> float:
    ndot_squared = ndot_direction * ndot_direction
    alpha_squared = alpha * alpha
    denominator = ndot_direction + math.sqrt(
        max(alpha_squared + (1.0 - alpha_squared) * ndot_squared, 0.0)
    )
    return 2.0 * ndot_direction / denominator if denominator > 0.0 else 0.0


def fresnel_schlick(cos_theta: float, f0: float) -> float:
    return f0 + (1.0 - f0) * max(0.0, min(1.0, 1.0 - cos_theta)) ** 5


def integrate_channel(
    ndotv: float, roughness: float, f0: float, z_samples: int, phi_samples: int
) -> float:
    if roughness <= 0.001:
        return fresnel_schlick(ndotv, f0)

    alpha = roughness * roughness
    alpha_squared = alpha * alpha
    view_x = math.sqrt(max(0.0, 1.0 - ndotv * ndotv))
    view_z = ndotv
    g1_view = smith_g1_ggx(ndotv, alpha)
    sample_weight = 2.0 * math.pi / float(z_samples * phi_samples)
    integral = 0.0

    for z_index in range(z_samples):
        ndotl = (float(z_index) + 0.5) / float(z_samples)
        radial = math.sqrt(max(0.0, 1.0 - ndotl * ndotl))
        g1_light = smith_g1_ggx(ndotl, alpha)
        for phi_index in range(phi_samples):
            phi = 2.0 * math.pi * (float(phi_index) + 0.5) / float(phi_samples)
            light_x = radial * math.cos(phi)
            light_y = radial * math.sin(phi)
            half_x = view_x + light_x
            half_y = light_y
            half_z = view_z + ndotl
            half_length = math.sqrt(half_x * half_x + half_y * half_y + half_z * half_z)
            if half_length <= 0.0:
                continue
            ndoth = max(0.0, half_z / half_length)
            vdoth = max(0.0, (view_x * half_x + view_z * half_z) / half_length)
            distribution_denominator = ndoth * ndoth * (alpha_squared - 1.0) + 1.0
            distribution = alpha_squared / (
                math.pi * distribution_denominator * distribution_denominator
            )
            geometry = g1_view * g1_light
            fresnel = fresnel_schlick(vdoth, f0)
            brdf = distribution * geometry * fresnel / max(4.0 * ndotv * ndotl, 1.0e-6)
            integral += brdf * ndotl * sample_weight

    return integral


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Integrate the Hybrid Reflection Cook-Torrance model under constant white radiance."
    )
    parser.add_argument("report", type=Path, help="HDR diagnostic JSON with a referenceSurfaceSample")
    parser.add_argument("--z-samples", type=int, default=512)
    parser.add_argument("--phi-samples", type=int, default=2048)
    args = parser.parse_args()

    report = json.loads(args.report.read_text(encoding="utf-8"))
    if report.get("specularEstimateIncidentRadiance") != "constant-white-1":
        raise ValueError("Report does not use the constant-white estimator diagnostic mode.")
    surface = report["referenceSurfaceSample"]
    if not surface.get("valid", False):
        raise ValueError("Report does not contain a valid reference surface sample.")

    reference_rgb = [
        integrate_channel(
            float(surface["ndotv"]),
            float(surface["roughness"]),
            float(channel_f0),
            args.z_samples,
            args.phi_samples,
        )
        for channel_f0 in surface["f0"]
    ]
    reference_luminance = (
        0.2126 * reference_rgb[0] + 0.7152 * reference_rgb[1] + 0.0722 * reference_rgb[2]
    )
    measured_luminance = float(
        report["statistics"]["specularEstimate"]["temporalMeanLuminance"]
    )
    measured_standard_deviation = float(
        report["statistics"]["specularEstimate"]["temporalStandardDeviation"]
    )
    measurement_frames = int(report["measurementFrames"])
    measured_standard_error = measured_standard_deviation / math.sqrt(float(measurement_frames))
    absolute_error = measured_luminance - reference_luminance
    relative_error = (
        absolute_error / reference_luminance
        if abs(reference_luminance) > 1.0e-12
        else 0.0
    )
    result = {
        "reference": "independent-uniform-hemisphere-midpoint-integration",
        "zSamples": args.z_samples,
        "phiSamples": args.phi_samples,
        "referenceRgb": reference_rgb,
        "referenceLuminance": reference_luminance,
        "measuredLuminance": measured_luminance,
        "measurementFrames": measurement_frames,
        "measuredStandardDeviation": measured_standard_deviation,
        "measuredStandardError": measured_standard_error,
        "absoluteError": absolute_error,
        "relativeError": relative_error,
        "standardErrorUnits": absolute_error / measured_standard_error
        if measured_standard_error > 0.0
        else 0.0,
    }
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
