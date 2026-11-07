"""Compare finite sample budgets without claiming statistical convergence."""
import argparse
import json
from pathlib import Path

import numpy as np

from transmission_texture_fixture import write_json
from validate_part1 import sha
from validate_volume_furnace import summarize


def compare(a, b):
    for report in (a, b):
        if report.get("status") != "passed-controls-and-measured-finite-budgets":
            raise ValueError("Incomplete source report")
        summarize(report["cases"])
    if (a["hashes"]["executable"] != b["hashes"]["executable"]
            or a["hashes"]["shader"] != b["hashes"]["shader"]
            or a.get("windowSize") != b.get("windowSize") or a["frames"] >= b["frames"]):
        raise ValueError("Incompatible sample-budget comparison")
    right = {case["name"]: case for case in b["cases"]}
    results = []
    for left in a["cases"]:
        other = right[left["name"]]
        if (any(left[key] != other[key] for key in ("ior", "roughness", "bounces"))
                or left["capture"]["inputs"] != other["capture"]["inputs"]
                or left["assessment"]["roi"] != other["assessment"]["roi"]):
            raise ValueError("Different case inputs")
        delta = np.asarray(other["assessment"]["mean"]) - np.asarray(left["assessment"]["mean"])
        results.append(dict(name=left["name"], delta=delta.tolist(), maxAbsDelta=float(np.abs(delta).max())))
    return dict(status="compared-finite-sample-budgets", frames=[a["frames"], b["frames"]],
                cases=results, scope="Nested deterministic samples; no confidence interval or unbiasedness claim")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("first", type=Path)
    parser.add_argument("second", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    result = compare(*[json.loads(p.read_text(encoding="utf-8")) for p in (args.first, args.second)])
    result["sourceSha256"] = [sha(p) for p in (args.first, args.second)]
    if args.output.exists():
        raise ValueError("Output already exists")
    write_json(args.output, result)
