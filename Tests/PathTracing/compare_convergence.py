"""Compare two HDR capture reports against one shared finite-sample reference."""
import argparse
import hashlib
import json
from pathlib import Path

from compare_hdr import mean_image, read_pfm, rmse


def capture_pixels(record, roi):
    path = Path(record["path"])
    if hashlib.sha256(path.read_bytes()).hexdigest() != record["sha256"]:
        raise ValueError(f"Capture hash changed: {path}")
    return read_pfm(path, roi)


def metrics(images, reference):
    if len(images) < 2 or not reference or any(len(image) != len(reference) for image in images):
        raise ValueError("Need two or more equal-sized seed images and a reference")
    average = mean_image(images)
    variance = sum(sum((image[i] - average[i]) ** 2 for image in images) /
                   (len(images) - 1) for i in range(len(average))) / len(average)
    result = dict(meanImageRmse=rmse(average, reference), seedVariance=variance,
                perSeedRmse=[rmse(image, reference) for image in images],
                meanRadiance=sum(average) / len(average))
    if len(average) % 3 == 0:
        result["meanRgb"] = [sum(average[c::3]) / (len(average) // 3) for c in range(3)]
    result["seedCount"] = len(images)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--low-report", type=Path)
    parser.add_argument("--high-report", type=Path)
    parser.add_argument("--suite-report", type=Path, help="Plot a multi-sample suite without recapturing")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.suite_report:
        if args.low_report or args.high_report:
            parser.error("Suite and legacy two-report inputs are mutually exclusive")
        plot_suite(args.suite_report, args.output)
        return
    if not args.low_report or not args.high_report:
        parser.error("Supply --suite-report or both --low-report and --high-report")
    low = json.loads(args.low_report.read_text(encoding="utf-8"))
    high = json.loads(args.high_report.read_text(encoding="utf-8"))
    for key in ("sceneFileSha256", "renderPresetSha256", "roi", "seeds", "referenceSeeds",
                "directOnly", "referenceMode"):
        if low[key] != high[key]:
            raise ValueError(f"Capture conditions differ: {key}")
    if (not high["directOnly"] or high["referenceMode"] != 0 or len(high["seeds"]) < 2 or
            low["samples"] >= high["samples"] or high["samples"] >= high["referenceSamples"]):
        raise ValueError("Expected direct-only captures with increasing sample counts and a higher reference")
    roi = high["roi"]
    references = []
    for seed in high["referenceSeeds"]:
        matches = [record for record in high["captures"] if record["seed"] == seed and
                   record["samples"] == high["referenceSamples"] and record["mode"] == 0 and
                   Path(record["path"]).stem == f"reference-{seed}"]
        if len(matches) != 1:
            raise ValueError(f"Missing unique reference seed {seed}")
        references.append(capture_pixels(matches[0], roi)[1])
    if len(references) < 2:
        raise ValueError("At least two reference seeds are required")
    reference = mean_image(references)
    results = []
    for source in (low, high):
        images = []
        for seed in source["seeds"]:
            matches = [record for record in source["captures"] if record["seed"] == seed and
                       record["samples"] == source["samples"] and record["mode"] == 0 and
                       Path(record["path"]).stem == f"mode-0-seed-{seed}"]
            if len(matches) != 1:
                raise ValueError(f"Missing unique evaluation seed {seed}")
            images.append(capture_pixels(matches[0], roi)[1])
        results.append(dict(samples=source["samples"], **metrics(images, reference)))
    report = dict(schemaVersion=1, domain="linear-hdr-rgb", roi=roi,
                  sceneFileSha256=high["sceneFileSha256"],
                  renderPresetSha256=high["renderPresetSha256"],
                  evaluationSeeds=high["seeds"], referenceSeeds=high["referenceSeeds"],
                  referenceSamples=high["referenceSamples"],
                  referenceDisagreementRmse=rmse(references[0], references[1]),
                  limitation="Finite-sample reference; its seed disagreement is not a confidence bound.",
                  results=results)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"Shared {high['referenceSamples']} spp reference disagreement: {report['referenceDisagreementRmse']:.8g}")
    for result in results:
        print(f"{result['samples']} spp: RMSE={result['meanImageRmse']:.8g}, "
              f"seed variance={result['seedVariance']:.8g}, mean radiance={result['meanRadiance']:.8g}")


def plot_suite(source, output):
    """Scientific plot artifacts; all series use the suite's common reference."""
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    report = json.loads(source.read_text())
    if report["status"] != "complete" or not report["results"]:
        raise ValueError("Cannot plot incomplete or smoke-only suite")
    output.mkdir(parents=True, exist_ok=True)
    for scene_id, rois in report["scenes"].items():
        fig, axes = plt.subplots(len(rois), 2, figsize=(10, 3 * len(rois)), squeeze=False)
        for row, label in enumerate(rois):
            reference = next(r for r in report["references"] if r["sceneId"] == scene_id and r["roiName"] == label)
            for mode in report["modes"]:
                series = sorted((r for r in report["results"] if r["sceneId"] == scene_id and
                    r["roiName"] == label and r["mode"] == mode), key=lambda r: r["samples"])
                name = {0: "Local light paths", 1: "BSDF", 2: "NEE", 5: "MIS"}[mode]
                for column, key in enumerate(("meanImageRmse", "seedVariance")):
                    axes[row, column].plot([r["samples"] for r in series], [r[key] for r in series], "o-", label=name)
            disagreement = reference["pairwiseDisagreementRmse"][0]
            if disagreement > 0:
                axes[row, 0].axhline(disagreement, color="gray", linestyle="--", label="Reference seed disagreement")
            else:
                axes[row, 0].text(.03, .95, "Reference seed disagreement = 0", transform=axes[row, 0].transAxes, va="top", fontsize=8)
            for column, axis in enumerate(axes[row]):
                axis.set_xscale("log", base=2)
                positive = any(any(value > 0 for value in line.get_ydata()) for line in axis.lines)
                axis.set_yscale("log" if positive else "linear")
                axis.set_xlabel("Accumulated samples / pixel")
                axis.set_ylabel("Mean-image RGB RMSE" if column == 0 else "Mean unbiased seed variance")
                axis.set_title(label)
                axis.grid(True, alpha=.25)
                axis.legend(fontsize=8)
        bounces = next(c["diagnostics"]["maxBounces"] for c in report["captures"] if c["sceneId"] == scene_id)
        fig.suptitle(f"{scene_id}: static, {bounces} bounces; {len(report['seeds'])} seeds; finite {report['referenceSamples']} spp reference")
        fig.tight_layout()
        fig.savefig(output / (scene_id + ".png"), dpi=160)
        fig.savefig(output / (scene_id + ".svg"))
        plt.close(fig)


if __name__ == "__main__":
    main()
