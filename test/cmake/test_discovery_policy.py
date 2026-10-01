"""Validate classification in CTest's independent script policy environment."""

import argparse
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


SCRIPT = Path(__file__).resolve().with_name("classify_tests.cmake")
BASELINE = SCRIPT.with_name("baseline_tests.txt").read_text("utf-8").splitlines()
CTEST = "ctest"


class DiscoveryPolicyTests(unittest.TestCase):
    """Exercise CTest registration without executing protocol placeholder cases."""

    def discover(self, names):
        with tempfile.TemporaryDirectory(prefix="xgl-discovery-policy-") as directory:
            root = Path(directory)
            commands = ["set(xgl_discovered_tests)"]
            for name in names:
                commands.append(f'list(APPEND xgl_discovered_tests "{name}")')
                commands.append(f'add_test("{name}" "${{CMAKE_COMMAND}}" -E true)')
            commands.append(f'include("{SCRIPT.as_posix()}")')
            (root / "CTestTestfile.cmake").write_text("\n".join(commands) + "\n", "utf-8")
            return subprocess.run(
                [CTEST, "--test-dir", str(root), "--show-only=json-v1"],
                text=True, capture_output=True, check=False,
            )

    def test_complete_historical_registry_is_classified_without_project_policies(self):
        result = self.discover(BASELINE)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        cases = json.loads(result.stdout)["tests"]
        self.assertEqual({case["name"] for case in cases}, set(BASELINE))
        labels = {
            case["name"]: next(p["value"] for p in case["properties"] if p["name"] == "LABELS")
            for case in cases
        }
        self.assertEqual(set(labels["XglTypesTest.FrameHeaderSize"]), {"unit", "xgl"})
        self.assertEqual(set(labels["XglIntegrationTest.BasicInstanceLifecycle"]),
                         {"integration", "xgl"})
        self.assertEqual(set(labels["XglFrameProperties.CrcErrorDetection"]),
                         {"unit", "property", "xgl"})

    def test_missing_historical_case_is_rejected_by_name(self):
        result = self.discover(BASELINE[1:])
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Missing historical GoogleTest case: " + BASELINE[0],
                      " ".join(result.stderr.split()))

    def test_empty_registry_is_rejected(self):
        result = self.discover([])
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Missing historical GoogleTest case:", result.stderr)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ctest", default="ctest")
    options, remaining = parser.parse_known_args()
    CTEST = options.ctest
    unittest.main(argv=[sys.argv[0], *remaining])
