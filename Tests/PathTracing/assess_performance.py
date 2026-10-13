"""Reject incomplete or unstable timing cohorts before reporting scaling ratios."""
import argparse
import json
from pathlib import Path
import statistics

from measure_performance import summarize


def assess(report):
    if report.get("status") != "complete" or report.get("failures"):
        raise ValueError("Only complete, failure-free campaigns can be assessed")
    repeats = report["repeats"]
    frames = report["measurementObservations"]
    if repeats < 3 or frames < 64 or report["warmupObservations"] < 64:
        raise ValueError("Require at least three repeats, 64 warm-up and 64 measurement observations")
    names = [case["name"] for case in report["cases"]]
    if len(set(names)) != len(names) or "baseline" not in names:
        raise ValueError("Need distinct cases including baseline")
    if len(report["runs"]) != len(names) * repeats:
        raise ValueError("Incomplete run matrix")
    results = []
    for case in report["cases"]:
        runs = [run for run in report["runs"] if run["case"] == case["name"]]
        if sorted(run["repeat"] for run in runs) != list(range(repeats)):
            raise ValueError("Missing or duplicate repeat")
        stats = []
        for run in runs:
            if len(run["values"]) != frames:
                raise ValueError("Incomplete observations")
            stats.append(summarize(run["values"]))
        medians = [stat["medianMs"] for stat in stats]
        center = statistics.median(medians)
        spread = (max(medians) - min(medians)) / center
        tails = [stat["p95Ms"] / stat["medianMs"] for stat in stats]
        stable = spread <= 0.10 and max(tails) <= 1.25
        results.append(dict(case=case["name"], medianOfRunMediansMs=center,
                            runMedianSpread=spread, maxP95OverMedian=max(tails), stable=stable,
                            primarySamplesPerSecond=case["width"] * case["height"] *
                            case["samplesPerFrame"] * 1000 / center))
    baseline = next(result for result in results if result["case"] == "baseline")
    for result in results:
        result["acceptedTimeRatioToBaseline"] = (result["medianOfRunMediansMs"] /
            baseline["medianOfRunMediansMs"] if baseline["stable"] and result["stable"] else None)
    return dict(status="accepted" if all(result["stable"] for result in results) else "inconclusive",
                runMedianSpreadLimit=0.10, p95OverMedianLimit=1.25, cases=results,
                limitation="Diagnostic stability policy, not a statistical confidence interval. Debug pass GPU time excludes rebuild, reset, capture and other passes; primary samples are not ray counts.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("report", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    result = assess(json.loads(args.report.read_text(encoding="utf-8")))
    text = json.dumps(result, indent=2) + "\n"
    args.output.write_bytes(text.replace("\n", "\r\n").encode("utf-8"))
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
