"""Run Parts 1-5 serially so GPU validation processes do not overlap."""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import sys

from validate_part1 import write_json, sha
from assess_scale_policy import assess

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--parts", nargs="+", choices=["part1", "part2", "part3", "part3-extra", "part4", "part5", "part5-controls", "transport"])
    args = parser.parse_args()
    output = args.output.resolve()
    if output.exists() and any(output.iterdir()):
        parser.error("Output must be empty")
    output.mkdir(parents=True)
    commit = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
    tasks = [
        ("part1", "validate_part1.py", ["--samples", "8"]),
        ("part2", "run_convergence_suite.py", ["--samples", "4", "16", "--reference-samples", "32",
            "--seeds", "1", "2", "--reference-seeds", "101", "102", "--modes", "1", "5"]),
        ("part3", "validate_scale.py", ["--samples", "4"]),
        ("part3-extra", "complete_scale_suite.py", ["--powershell", shutil.which("pwsh") or "pwsh"]),
        ("part4", "measure_performance.py", ["--smoke", "--warmup", "8", "--frames", "16", "--repeats", "1"]),
        ("part5", "validate_inputs.py", ["--base-commit", commit]),
        ("part5-controls", "validate_inputs.py", ["--base-commit", commit, "--cases",
            "input-shifted-ViewZ-moving,input-camera-transform-ViewZ-static"]),
        ("transport", "validate_transport.py", ["--samples", "64"]),
    ]
    if args.parts:
        tasks = [task for task in tasks if task[0] in args.parts or
                 (task[0] == "part5-controls" and "part5" in args.parts) or
                 (task[0] == "part3-extra" and "part3" in args.parts)]
    report = dict(schemaVersion=1, status="running", testedCommit=commit,
                  exeHash=sha(ROOT / "bin/x64/Debug/RtPbrSurvey.exe"), runs=[])
    for name, script, options in tasks:
        command = [sys.executable, "-B", str(ROOT / "Tests/PathTracing" / script),
                   "--output", str(output / name), *options]
        print("Starting " + name, flush=True)
        with (output / (name + "-runner.log")).open("w", encoding="utf-8") as log:
            result = subprocess.run(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT)
        run = dict(name=name, command=command, exitCode=result.returncode,
                   reportPath=str(output / name / "report.json"))
        if name == "part3" and result.returncode == 0:
            policy = assess(json.loads(Path(run["reportPath"]).read_text(encoding="utf-8-sig")))
            write_json(output / name / "policy-assessment.json", policy)
            run["policyStatus"] = policy["status"]
            if policy["status"] != "passed":
                run["exitCode"] = 1
        if name == "part3-extra" and result.returncode == 0:
            extra = json.loads(Path(run["reportPath"]).read_text(encoding="utf-8-sig"))
            if (extra.get("failures") or len(extra.get("tmax", [])) != 6 or
                    len(extra.get("nearLight", [])) != 4 or
                    any(r["exitCode"] != 0 for r in extra.get("nearLight", []))):
                run.update(exitCode=1, failure="Supplementary scale campaign failed or is incomplete")
        report["runs"].append(run)
        write_json(output / "report.json", report)
        print(f"Finished {name}: exit={run['exitCode']}", flush=True)
    report["status"] = "failed" if any(run["exitCode"] for run in report["runs"]) else "complete"
    write_json(output / "report.json", report)
    return 0 if report["status"] == "complete" else 1


if __name__ == "__main__":
    raise SystemExit(main())
