from contextlib import redirect_stdout
import io
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

from run_completion_regression import main
from validate_part1 import write_json


class CompletionRegressionTests(unittest.TestCase):
    def run_suite(self, parts, child_report):
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary) / "run"
            commands = []
            def child(command, **kwargs):
                commands.append(command)
                destination = Path(command[command.index("--output")+1])
                write_json(destination / "report.json", child_report)
                return subprocess.CompletedProcess(command, 0)
            with patch("sys.argv", ["runner", "--output", str(output), "--parts", *parts]), \
                    patch("run_completion_regression.subprocess.check_output", return_value="base\n"), \
                    patch("run_completion_regression.sha", return_value="hash"), \
                    patch("run_completion_regression.subprocess.run", side_effect=child), \
                    redirect_stdout(io.StringIO()):
                status = main()
            return status, json.loads((output / "report.json").read_text()), commands

    def test_supplementary_report_failure_overrides_success_exit(self):
        status, report, _ = self.run_suite(["part3-extra"], dict(failures=["failed capture"], tmax=[], nearLight=[]))
        self.assertEqual(status, 1)
        self.assertEqual(report["runs"][0]["exitCode"], 1)

    def test_complete_supplementary_report_passes(self):
        status, _, _ = self.run_suite(["part3-extra"], dict(failures=[], tmax=[{}]*6,
            nearLight=[dict(exitCode=0)]*4))
        self.assertEqual(status, 0)

    def test_part5_also_selects_viewz_controls(self):
        status, report, commands = self.run_suite(["part5"], dict(status="done", failures=[]))
        self.assertEqual(status, 0)
        self.assertEqual([r["name"] for r in report["runs"]], ["part5", "part5-controls"])
        self.assertIn("input-camera-transform-ViewZ-static", commands[1][-1])

    def test_part3_supported_policy_failure_is_propagated(self):
        status, report, _ = self.run_suite(["part3"], dict(status="complete", failures=[],
            visibility=[], contact=[], selfIntersection=[], tmax=[{}]*6, nearLight=[dict(exitCode=0)]*4))
        self.assertEqual(status, 1)
        self.assertEqual(report["runs"][0]["policyStatus"], "failed")
        self.assertEqual(report["runs"][1]["name"], "part3-extra")
        self.assertEqual(report["runs"][1]["exitCode"], 0)


if __name__ == "__main__":
    unittest.main()
