"""Separate the supported relative-offset policy from fixed/zero negative controls."""
import argparse
import json
import math
from pathlib import Path

from validate_part1 import write_json


def assess(report):
    failures = []
    scales = (.01, 1., 100.)
    if report.get("status") != "complete" or report.get("failures"):
        failures.append("Scale capture campaign did not complete cleanly")
    visibility = {(r["scale"], r["angle"]): r for r in report.get("visibility", []) if r["policy"] == "relative"}
    contact = {r["scale"]: r for r in report.get("contact", []) if r["policy"] == "relative"}
    self_hits = {r["scale"]: r for r in report.get("selfIntersection", []) if r["policy"] == "relative"}
    for scale in scales:
        for angle in ("front", "angled"):
            if not visibility.get((scale, angle), {}).get("passed", False):
                failures.append(f"Relative visibility failed/missing: {scale}/{angle}")
        item = contact.get(scale, {})
        if item.get("shadowColumnCount", 0) <= 0:
            failures.append(f"Relative contact shadow failed/missing: {scale}")
        item = self_hits.get(scale, {})
        ratio, dark = item.get("meanRatio", float("nan")), item.get("darkenedChannelFraction", float("nan"))
        if not (math.isfinite(ratio) and math.isfinite(dark) and .99 <= ratio <= 1.01 and 0 <= dark <= .01):
            failures.append(f"Relative convex self-hit failed/missing: {scale}")
    return dict(status="failed" if failures else "passed", failures=failures,
        supportedPolicy=dict(normalBias="0.01 * sceneScale", rayTMin="0.001 * sceneScale",
            rayTMax="10000 * sceneScale", testedScales=list(scales)),
        fixedVisibilityFailures=[r for r in report.get("visibility", []) if r["policy"] == "fixed" and not r["passed"]],
        zeroOffsetControls=[r for r in report.get("selfIntersection", []) if r["policy"] == "zero"],
        limitation="Policy validated only on the scaled fixture family near the origin. Not an automatic scene-size heuristic or guarantee for thin geometry, grazing rays, or large world-coordinate translations.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--report", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    result = assess(json.loads(args.report.read_text(encoding="utf-8-sig")))
    write_json(args.output, result)
    print(json.dumps(result))
    return 0 if result["status"] == "passed" else 1


if __name__ == "__main__":
    raise SystemExit(main())
