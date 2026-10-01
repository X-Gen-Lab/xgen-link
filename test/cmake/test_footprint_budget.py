"""Check that resource gates reject growth and incomplete ELF evidence."""

import copy
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]


class FootprintBudget(unittest.TestCase):
    """Exercise the actual budget command with bounded audit report fixtures."""

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="xgl-footprint-policy-")
        self.directory = Path(self.temporary.name)
        self.policy = {
            "schema_version": 1,
            "compiler_version": "15.2.1",
            "configuration": {"profile": "boot", "frame_bytes": 128},
            "baseline": {"flash_bytes": 15692, "workspace_bytes": 1168},
            "limits": {"flash_bytes": 15692, "workspace_bytes": 1168},
        }
        self.report = {
            "compiler": "arm-none-eabi-gcc 15.2.1",
            "build_parameters": {"XGL_PROFILE": "boot"},
            "frame_bytes": 128,
            "flash_bytes": 15500,
            "workspace_bytes": 1160,
            "forbidden_heap_symbols": [],
            "unresolved_symbols": [],
        }

    def tearDown(self):
        self.temporary.cleanup()

    def run_policy(self, report=None):
        report_path = self.directory / "report.json"
        policy_path = self.directory / "policy.json"
        output_path = self.directory / "result.json"
        report_path.write_text(json.dumps(report or self.report), encoding="utf-8")
        policy_path.write_text(json.dumps(self.policy), encoding="utf-8")
        result = subprocess.run(
            [sys.executable, str(ROOT / "tools/boot_footprint/check_budget.py"),
             "--report", str(report_path), "--policy", str(policy_path),
             "--output", str(output_path)],
            capture_output=True, text=True, check=False)
        return result, output_path

    def test_reports_actual_delta_within_limits(self):
        result, output = self.run_policy()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        audit = json.loads(output.read_text())
        self.assertEqual(audit["status"], "passed")
        self.assertEqual(audit["deltas"]["workspace_bytes"], -8)

    def test_rejects_growth_beyond_a_metric_limit(self):
        self.report["flash_bytes"] = 15693
        result, output = self.run_policy()
        self.assertNotEqual(result.returncode, 0)
        audit = json.loads(output.read_text())
        self.assertEqual(audit["status"], "failed")
        self.assertTrue(any("flash_bytes" in item for item in audit["violations"]))

    def test_rejects_heap_and_unresolved_symbols(self):
        for field in ("forbidden_heap_symbols", "unresolved_symbols"):
            with self.subTest(field=field):
                report = copy.deepcopy(self.report)
                report[field] = ["malloc"]
                result, _ = self.run_policy(report)
                self.assertNotEqual(result.returncode, 0)

    def test_rejects_missing_measurements(self):
        del self.report["workspace_bytes"]
        result, _ = self.run_policy()
        self.assertNotEqual(result.returncode, 0)

    def test_rejects_mismatched_toolchain_or_configuration(self):
        for field, value in (("compiler", "arm-none-eabi-gcc 14.2.1"),
                             ("frame_bytes", 64)):
            with self.subTest(field=field):
                report = copy.deepcopy(self.report)
                report[field] = value
                result, _ = self.run_policy(report)
                self.assertNotEqual(result.returncode, 0)


if __name__ == "__main__":
    unittest.main()
