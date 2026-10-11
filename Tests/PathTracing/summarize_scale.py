"""Summarize Part 3 measurements and plot diagnostic HDR profiles (matplotlib)."""
import argparse
import hashlib
import json
from pathlib import Path
import sys

from compare_hdr import read_pfm, rmse
from validate_part1 import write_json
from validate_scale import ROOT, contact_roi_contract


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--main", type=Path, required=True)
    parser.add_argument("--extra", type=Path, required=True)
    parser.add_argument("--packages", type=Path)
    args = parser.parse_args()
    args.main = args.main.resolve()
    args.extra = args.extra.resolve()
    if args.packages:
        sys.path.insert(0, str(args.packages.resolve()))
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    import numpy as np
    report = json.loads((args.main / "report.json").read_text())
    extra = json.loads((args.extra / "report.json").read_text())
    if report["status"] != "complete":
        raise ValueError("Main suite has not completed")
    plot_dir = args.main / "plots"
    plot_dir.mkdir(exist_ok=True)
    fig, axes = plt.subplots(1, 3, figsize=(15, 4), sharey=True)
    for axis, scale in zip(axes, report["scales"]):
        for row in report["contact"]:
            if row["scale"] == scale:
                axis.plot(range(1060, 1188), row["profile"], label=row["policy"])
        axis.set(title=f"Scale {scale}", xlabel="Pixel x", ylim=(-.05, 1.1))
        axis.grid(alpha=.3)
    axes[0].set_ylabel("Contact / clear floor radiance")
    axes[2].legend()
    fig.tight_layout()
    fig.savefig(plot_dir / "contact-profiles.png", dpi=160)
    fig.savefig(plot_dir / "contact-profiles.svg")
    plt.close(fig)
    fig, axes = plt.subplots(2, 3, figsize=(12, 5))
    for column, scale in enumerate(report["scales"]):
        clear_record = next(r for r in report["captures"] if r["scale"] == scale and r["angle"] == "angled"
            and r["variant"] == "clear" and r["policy"] == "relative")
        roi = [1030, 490, 200, 120]
        _, clear = read_pfm(Path(clear_record["path"]), roi)
        for row, policy in enumerate(("relative", "fixed")):
            record = next(r for r in report["captures"] if r["scale"] == scale and r["family"] == "contact" and r["policy"] == policy)
            _, pixels = read_pfm(Path(record["path"]), roi)
            base = np.asarray(clear).reshape(120, 200, 3).mean(axis=2)
            image = np.asarray(pixels).reshape(120, 200, 3).mean(axis=2)
            ratio = np.divide(image, base, out=np.zeros_like(image), where=base > 1e-8)
            axes[row, column].imshow(ratio, vmin=0, vmax=1, cmap="gray")
            axes[row, column].set(title=f"Scale {scale}, {policy}")
            axes[row, column].axis("off")
    fig.suptitle("HDR contact / clear ratio crop; black=shadow, white=lit (cube silhouette may appear)")
    fig.tight_layout()
    fig.savefig(plot_dir / "contact-ratio-crops.png", dpi=160)
    plt.close(fig)
    fig, axes = plt.subplots(2, 3, figsize=(10, 6))
    for column, scale in enumerate(report["scales"]):
        reference = next(r for r in report["captures"] if r["scale"] == scale and r["family"] == "self"
            and r["variant"] == "unshadowed")
        roi = report["rois"]["selfIntersection"]
        _, clear = read_pfm(Path(reference["path"]), roi)
        base = np.asarray(clear).reshape(roi[3], roi[2], 3).mean(axis=2)
        for row, policy in enumerate(("relative", "zero")):
            record = next(r for r in report["captures"] if r["scale"] == scale and r["family"] == "self"
                and r["policy"] == policy and r["variant"] == "shadowed")
            _, pixels = read_pfm(Path(record["path"]), roi)
            image = np.asarray(pixels).reshape(roi[3], roi[2], 3).mean(axis=2)
            ratio = np.divide(image, base, out=np.zeros_like(image), where=base > 1e-8)
            axes[row, column].imshow(ratio, vmin=0, vmax=1, cmap="gray", interpolation="nearest")
            axes[row, column].set(title=f"Scale {scale}, {policy}")
            axes[row, column].axis("off")
    fig.suptitle("Convex sphere / unshadowed HDR ratio; dark pixels indicate self-hit loss")
    fig.tight_layout()
    fig.savefig(plot_dir / "self-hit-ratio.png", dpi=160)
    plt.close(fig)
    agreement = []
    for angle in ("front", "angled"):
        baseline = next(r for r in report["captures"] if r["scale"] == 1 and r["angle"] == angle
            and r["variant"] == "clear" and r["policy"] == "relative")
        _, reference = read_pfm(Path(baseline["path"]), report["rois"]["visibility"])
        for scale in report["scales"]:
            record = next(r for r in report["captures"] if r["scale"] == scale and r["angle"] == angle
                and r["variant"] == "clear" and r["policy"] == "relative")
            _, pixels = read_pfm(Path(record["path"]), report["rois"]["visibility"])
            difference = abs(sum(pixels)/sum(reference)-1)
            agreement.append(dict(scale=scale, angle=angle, meanRatio=sum(pixels)/sum(reference),
                rmse=rmse(pixels, reference), passed=difference <= .01))
    summary = dict(schemaVersion=1, part=3, baseCommit=report["commit"], testedCommit=report["commit"],
        rendererBuildCommit="302a057", workspace=str(ROOT), branch=report["branch"], gpuDriver=report["gpuDriver"],
        build=report["build"], samples=report["samples"], seed=report["seed"], rois=report["rois"],
        scaleIllumination=report["scaleIllumination"], captureCount=len(report["captures"])+len(extra["tmax"])+12,
        failures=report["failures"]+extra["failures"], visibility=report["visibility"], scaleAgreement=agreement,
        contact=[{k:v for k,v in row.items() if k != "profile"} for row in report["contact"]],
        selfIntersection=report["selfIntersection"], contactRoiContract=contact_roi_contract(),
        tmax=[{k:r[k] for k in ("scale", "normalizedTMax", "mean", "command", "path", "sha256")} for r in extra["tmax"]],
        nearLight=[dict(command=r["command"], exitCode=r["exitCode"], report=r["report"],
            metrics={k:r["metrics"][k] for k in ("lightType", "samples", "seed", "normalBias", "lightOffsetX",
                "roi", "clearMean", "betweenMean", "beyondMean", "maxBeyondDifference")}) for r in extra["nearLight"]],
        thresholds=report["thresholds"],
        mainReport=str((args.main / "report.json").relative_to(ROOT)),
        extraReport=str((args.extra / "report.json").relative_to(ROOT)))
    summary["captureCommandsAndInputHashes"] = [{k: r[k] for k in ("name", "command", "sceneHash", "presetHash", "shadow")}
        for r in report["captures"]]
    source_paths = [ROOT / "bin/x64/Debug/RtPbrSurvey.exe"] + list((ROOT / "Tests/PathTracing").glob("*scale*.py"))
    source_paths += [ROOT / "Tests/PathTracing/Test-LocalLightVisibility.ps1"]
    summary["measurementCodeHashes"] = [dict(path=str(p.relative_to(ROOT)), sha256=hashlib.sha256(p.read_bytes()).hexdigest())
        for p in source_paths]
    summary["d3d12ErrorCount"] = sum(sum(line.startswith(("[ERROR]", "[CORRUPTION]")) for line in
        p.read_text(encoding="utf-8-sig").splitlines()) for directory in (args.main, args.extra) for p in directory.rglob("*.log"))
    summary["assessment"] = dict(status="done", rendererFixed=False,
        relativeVisibilityPassed=all(r["passed"] for r in report["visibility"] if r["policy"] == "relative"),
        reproducedFixedBiasFailures=[r for r in report["visibility"] if not r["passed"]],
        nearLightPassed=all(r["exitCode"] == 0 for r in extra["nearLight"]),
        limitations="One GPU, one seed, 4 spp; mixed-scale geometry and translated/nonuniform transforms untested.")
    summary["artifacts"] = [dict(path=str(p.relative_to(ROOT)), sha256=hashlib.sha256(p.read_bytes()).hexdigest())
        for directory in (args.main, args.extra) for p in directory.rglob("*") if p.is_file()]
    write_json(ROOT / "doc/branch/feature/path-tracing-validation-results/part-3-summary.json", summary)
    print(json.dumps(dict(visibility=summary["visibility"], scaleAgreement=agreement,
        contact=[{k:v for k,v in r.items() if k != "profile"} for r in report["contact"]],
        selfIntersection=report["selfIntersection"], tmax=[{k:r[k] for k in ("scale","normalizedTMax","mean")} for r in extra["tmax"]])))


if __name__ == "__main__":
    main()
