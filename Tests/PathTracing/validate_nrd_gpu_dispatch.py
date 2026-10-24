"""Validate real RELAX dispatch outputs, repeatability and excluded-input behavior."""
import argparse
import json
from pathlib import Path

import numpy as np

from validate_denoiser_distances import capture
from validate_part1 import sha, write_json

ROOT = Path(__file__).resolve().parents[2]


def assess_output(source, output, admission):
    if source.shape != output.shape or source.shape != admission.shape or output.ndim != 3 or output.shape[-1] != 4:
        raise ValueError("Mismatched GPU output dimensions")
    if not np.isfinite(output).all() or (output < 0).any():
        raise ValueError("Nonfinite or negative RELAX output")
    accepted = admission[..., 0] == 1
    if accepted.sum() < 100 or not (output[accepted, :3] > 0).any():
        raise ValueError("No admitted RELAX output signal")
    changed = np.any(source[..., :3] != output[..., :3], axis=-1) & accepted
    if changed.sum() < 100:
        raise ValueError("RELAX output is indistinguishable from passthrough")
    if (output[~accepted] != 0).any():
        raise ValueError("RELAX wrote excluded pixels")
    return dict(admittedPixels=int(accepted.sum()), changedPixels=int(changed.sum()),
                maximum=float(output.max()), meanRgb=float(output[accepted, :3].mean()))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--exe", type=Path, default=ROOT / "build/Debug/RtPbrSurvey.exe")
    parser.add_argument("--shader-directory", type=Path, default=ROOT / "build/shaders")
    args = parser.parse_args()
    source, folder = args.source.resolve(strict=True), args.output.resolve()
    if source == folder or source in folder.parents or (folder.exists() and any(folder.iterdir())):
        parser.error("Output must be a separate empty directory")
    scene = json.loads((source / "scene.json").read_text())
    preset = json.loads((source / ("preset.json" if (source / "preset.json").exists() else "on.json")).read_text())
    preset["pathTracing"].update(accumulate=True, samplesPerFrame=1, pixelCenterPrimary=True, denoiserDistanceDiagnostics=True)
    write_json(folder / "scene.json", scene)
    write_json(folder / "preset.json", preset)
    excluded = json.loads(json.dumps(preset))
    excluded["pathTracing"]["denoiserDistanceDiagnostics"] = False
    write_json(folder / "excluded.json", excluded)
    exe = args.exe.resolve(strict=True)
    shader_directory = args.shader_directory.resolve(strict=True)
    manifest = shader_directory / "nrd-relax-manifest.txt"
    identifiers = manifest.read_text().splitlines()
    report = dict(status="running", fullIntegrationReady=False, records=[], failures=[],
        exeSha256=sha(exe), sceneSha256=sha(folder / "scene.json"), presetSha256=sha(folder / "preset.json"),
        scriptSha256=sha(Path(__file__)), manifestSha256=sha(manifest),
        shaders=[dict(identifier=name, sha256=sha(shader_directory / f"nrd-relax-{i}.cso"))
                 for i, name in enumerate(identifiers)])
    write_json(folder / "report.json", report)
    try:
        buffers, common = {}, None
        for name in ("NrdInputDiffuse", "NrdInputSpecular", "NrdInputAdmission", "NrdOutputDiffuse", "NrdOutputSpecular"):
            (meta, values), record = capture(exe, folder, name, folder / "preset.json", name)
            report["records"].append(record)
            if meta["resource"] != "PathTracing." + name or meta["format"] != 2:
                raise ValueError("Wrong capture identity/format")
            comparable = {key: value for key, value in meta.items() if key != "resource"}
            if common is not None and comparable != common:
                raise ValueError("GPU capture cohort mismatch")
            common, buffers[name] = comparable, values
            write_json(folder / "report.json", report)
        report["assessment"] = {}
        for lobe in ("Diffuse", "Specular"):
            name = "NrdOutput" + lobe
            report["assessment"][lobe] = assess_output(buffers["NrdInput" + lobe], buffers[name], buffers["NrdInputAdmission"])
            (meta, repeat), record = capture(exe, folder, name, folder / "preset.json", name + "-repeat")
            report["records"].append(record)
            if meta["resource"] != "PathTracing." + name or not np.array_equal(repeat, buffers[name]):
                raise ValueError("RELAX deterministic replay mismatch")
            (meta, empty), record = capture(exe, folder, name, folder / "excluded.json", name + "-excluded")
            report["records"].append(record)
            if meta["resource"] != "PathTracing." + name or not np.isfinite(empty).all() or (empty != 0).any():
                raise ValueError("Excluded inputs retained RELAX output")
            write_json(folder / "report.json", report)
        report["status"] = "passed"
    except Exception as error:
        report.update(status="failed", failures=[str(error)])
    report["limitation"] = "GPU dispatch and lifecycle diagnostics; not a visual-quality or final-composition acceptance."
    write_json(folder / "report.json", report)
    print(report["status"], report.get("assessment"), report["failures"])
    return report["status"] != "passed"


if __name__ == "__main__":
    raise SystemExit(main())
