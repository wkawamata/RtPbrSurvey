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
    average = mean_image(images)
    variance = sum(sum((image[i] - average[i]) ** 2 for image in images) /
                   (len(images) - 1) for i in range(len(average))) / len(average)
    return dict(meanImageRmse=rmse(average, reference), seedVariance=variance,
                perSeedRmse=[rmse(image, reference) for image in images],
                meanRadiance=sum(average) / len(average))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--low-report", type=Path, required=True)
    parser.add_argument("--high-report", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
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


if __name__ == "__main__":
    main()
