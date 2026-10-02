"""Plot Part 4 GPU timings and save compact reproducible result metadata."""
import argparse
import hashlib
import json
from pathlib import Path
import sys

from measure_performance import ROOT
from validate_part1 import write_json


def telemetry_summary(observations):
    states, temperatures, clocks, memory_clocks = [], [], [], []
    for observation in observations:
        fields = observation["gpu"].split(", ")
        if observation["exitCode"] != 0 or len(fields) != 6:
            continue
        states.append(fields[1])
        for values, field in ((temperatures, fields[2]), (clocks, fields[3]), (memory_clocks, fields[4])):
            try:
                values.append(float(field.split()[0]))
            except ValueError:
                pass
    return dict(observationCount=len(observations), pStates=sorted(set(states)),
        temperatureRangeC=[min(temperatures), max(temperatures)] if temperatures else None,
        smClockRangeMHz=[min(clocks), max(clocks)] if clocks else None,
        memoryClockRangeMHz=[min(memory_clocks), max(memory_clocks)] if memory_clocks else None)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--packages", type=Path)
    args = parser.parse_args()
    directory = args.output.resolve()
    report = json.loads((directory / "report.json").read_text())
    if report["status"] != "complete" or len(report["runs"]) != len(report["cases"])*report["repeats"]:
        raise ValueError("Suite has not completed")
    if args.packages:
        sys.path.insert(0, str(args.packages.resolve()))
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    cases = {r["name"]:r for r in report["cases"]}
    aggregates = {r["case"]:r for r in report["aggregates"]}
    groups = [
        ("Resolution", ["resolution-0", "resolution-1", "baseline"], ["960x540", "1280x720", "1920x1080"]),
        ("Samples/frame", ["baseline", "samplesPerFrame-0", "samplesPerFrame-1"], ["1", "2", "4"]),
        ("Bounce limit", ["maxBounces-0", "baseline", "maxBounces-1"], ["1", "2", "4"]),
        ("Co-located lights", ["baseline", "lights-0", "lights-1"], ["1", "4", "8"]),
        ("Unique mesh triangles", ["geometry-0", "baseline", "geometry-1"], ["386", "1538", "24578"])]
    plot_dir = directory / "plots"
    plot_dir.mkdir(exist_ok=True)
    fig, axes = plt.subplots(2, 3, figsize=(14, 8))
    for axis, (title, names, labels) in zip(axes.flat, groups):
        median = [aggregates[name]["medianMs"] for name in names]
        tail = [aggregates[name]["p95Ms"] for name in names]
        axis.bar(range(len(names)), median, color="#537ca6", label="Pooled median")
        axis.plot(range(len(names)), tail, "o--", color="#c87830", label="Pooled p95")
        for index, name in enumerate(names):
            runs = [r["stats"]["medianMs"] for r in report["runs"] if r["case"] == name]
            axis.scatter([index]*len(runs), runs, marker="x", color="black", label="Run medians" if index == 0 else None)
        axis.set(title=title, ylabel="PathTracingPass GPU ms", xticks=range(len(names)), xticklabels=labels)
        axis.grid(axis="y", alpha=.25)
    axes.flat[-1].axis("off")
    handles, labels = axes.flat[0].get_legend_handles_labels()
    axes.flat[-1].legend(handles, labels, loc="center")
    fig.suptitle(f"Debug x64, RTX 3080 Laptop; {report['repeats']} runs x {report['measurementObservations']} observations; clocks not locked")
    fig.tight_layout()
    fig.savefig(plot_dir / "gpu-performance.png", dpi=160)
    fig.savefig(plot_dir / "gpu-performance.svg")
    plt.close(fig)
    fig, axes = plt.subplots(len(groups), 1, figsize=(12, 12), sharex=True)
    for axis, (title, names, labels) in zip(axes, groups):
        for name, label in zip(names, labels):
            for run in (r for r in report["runs"] if r["case"] == name):
                axis.plot(run["values"], alpha=.65, label=f"{label}, run {run['repeat']+1}")
        axis.set(title=title, ylabel="GPU ms")
        axis.grid(alpha=.25)
        axis.legend(ncol=3, fontsize=7)
    axes[-1].set_xlabel(f"Measurement observation after {report['warmupObservations']} discarded observations")
    fig.tight_layout()
    fig.savefig(plot_dir / "gpu-timelines.png", dpi=140)
    plt.close(fig)
    summary = {k:report[k] for k in ("schemaVersion", "part", "baseCommit", "rendererBuildCommit", "branch", "workspace", "build",
        "warmupObservations", "measurementObservations", "repeats", "seed", "timing", "exeSha256", "initialPower", "finalPower", "failures")}
    summary["complete"] = True
    summary["cases"] = []
    for name, case in cases.items():
        runs = [r for r in report["runs"] if r["case"] == name]
        summary["cases"].append(dict(settings=case, aggregate=aggregates[name],
            runStatistics=[r["stats"] for r in runs],
            measuredGpuState=telemetry_summary([v for r in runs for v in r["measurementGpuTelemetry"]])))
    summary["runs"] = [{k:r[k] for k in ("name", "case", "repeat", "command", "sceneSha256", "presetSha256", "stats",
        "firstMeasuredCpuFrame", "lastMeasuredCpuFrame", "totalValidObservations", "diagnostics", "powerBefore", "powerAfter")}
        for r in report["runs"]]
    summary["d3d12ErrorCount"] = sum(sum(line.startswith(("[ERROR]", "[CORRUPTION]")) for line in
        p.read_text(encoding="utf-8-sig").splitlines()) for p in directory.glob("*.log"))
    summary["artifacts"] = [dict(path=str(p.relative_to(ROOT)), sha256=hashlib.sha256(p.read_bytes()).hexdigest())
        for p in directory.rglob("*") if p.is_file()]
    summary["sourceHashes"] = [dict(path=str(p.relative_to(ROOT)), sha256=hashlib.sha256(p.read_bytes()).hexdigest())
        for p in (ROOT / "App/RtPbrSurveyApp.cpp", ROOT / "Tests/PathTracing/measure_performance.py",
            ROOT / "Tests/PathTracing/test_measure_performance.py", Path(__file__).resolve())]
    summary["limitations"] = ["Debug, VSync=1, balanced AC power, GPU clocks not locked; rankings may be confounded by P-state changes.",
        "Latest completed GPU observations have no independent GPU frame ID; p95 is an empirical observation percentile.",
        "Tessellation changes polygonal approximation; this is not TLAS instance count scaling.",
        "Static warm path excludes rebuild/reset/capture costs; measured primary samples are not actual ray counts."]
    destination = ROOT / "doc/branch/feature/path-tracing-validation-results/part-4-summary.json"
    write_json(destination, summary)
    lines = ["| Case | Median ms | P95 ms | Run medians ms | SM clock MHz |", "| --- | ---: | ---: | --- | --- |"]
    for case in summary["cases"]:
        lines.append(f"| {case['settings']['name']} | {case['aggregate']['medianMs']:.6f} | {case['aggregate']['p95Ms']:.6f} | "
            + ", ".join(f"{r['medianMs']:.6f}" for r in case["runStatistics"]) + f" | {case['measuredGpuState']['smClockRangeMHz']} |")
    (directory / "table.md").write_bytes(("\r\n".join(lines)+"\r\n").encode("utf-8"))
    table = directory / "table.md"
    summary["artifacts"] = [r for r in summary["artifacts"] if r["path"] != str(table.relative_to(ROOT))]
    summary["artifacts"].append(dict(path=str(table.relative_to(ROOT)), sha256=hashlib.sha256(table.read_bytes()).hexdigest()))
    write_json(destination, summary)
    print("\n".join(lines))


if __name__ == "__main__":
    main()
