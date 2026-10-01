"""Exercise the built host runner's discovery, language and replay contracts."""

import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET


class RunnerContracts(unittest.TestCase):
    """Keep actual CTest registration and executable behavior observable."""

    def run_binary(self, *arguments, seed=None):
        environment = os.environ.copy()
        environment.pop("XGL_PROPERTY_SEED", None)
        if seed is not None:
            environment["XGL_PROPERTY_SEED"] = seed
        return subprocess.run(
            [str(OPTIONS.binary), "--gtest_filter=XglFrameProperties.*", *arguments],
            text=True, capture_output=True, env=environment, check=False,
        )

    def test_discovery_exposes_labeled_individual_tests(self):
        result = subprocess.run(
            ["ctest", "--test-dir", str(OPTIONS.build_dir), "-C", OPTIONS.config,
             "--show-only=json-v1"],
            text=True, capture_output=True, check=True,
        )
        tests = json.loads(result.stdout)["tests"]
        cases = [case for case in tests if "." in case["name"]]
        self.assertGreaterEqual(len(cases), 510)
        self.assertNotIn("xgl_tests", {case["name"] for case in tests})
        labels_seen = set()
        for case in cases:
            labels = next((p["value"] for p in case["properties"]
                           if p["name"] == "LABELS"), [])
            self.assertIn("xgl", labels, case["name"])
            self.assertTrue({"unit", "integration"}.intersection(labels), case["name"])
            labels_seen.update(labels)
        self.assertTrue({"unit", "integration", "property"}.issubset(labels_seen))
        baseline = Path(__file__).with_name("baseline_tests.txt").read_text().splitlines()
        self.assertEqual(len(baseline), 510)
        self.assertFalse(set(baseline) - {case["name"] for case in cases})

    def test_compilation_uses_standard_cpp17(self):
        entries = json.loads((OPTIONS.build_dir / "compile_commands.json").read_text())
        commands = [entry["command"] for entry in entries
                    if entry["file"].endswith(".cpp") and "xgl_tests" in entry["command"]]
        self.assertTrue(commands)
        for command in commands:
            self.assertRegex(command, r"(?:-std=c\+\+17|/std:c\+\+17)")
            self.assertRegex(command, r"(?:-pedantic-errors|/permissive-)")

    def test_explicit_seed_is_recorded_and_replayable(self):
        with tempfile.TemporaryDirectory(prefix="xgl-seed-replay-") as directory:
            snapshots = []
            for index in range(2):
                report = Path(directory) / f"replay-{index}.xml"
                result = self.run_binary("--xgl_property_seed=713",
                                         f"--gtest_output=xml:{report}")
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertIn("XGL_PROPERTY_SEED=713", result.stdout)
                cases = ET.parse(report).findall(".//testcase")
                self.assertEqual(len(cases), 6)
                snapshot = {}
                for case in cases:
                    properties = {p.get("name"): p.get("value")
                                  for p in case.findall("properties/property")}
                    self.assertEqual(properties["xgl_property_seed"], "713")
                    snapshot[case.get("name")] = properties["xgl_property_stream"]
                snapshots.append(snapshot)
            self.assertEqual(snapshots[0], snapshots[1])

    def test_duplicate_and_invalid_environment_seed_are_rejected(self):
        duplicate = self.run_binary("--xgl_property_seed=1", "--xgl_property_seed=2")
        environment = self.run_binary(seed="invalid")
        missing = self.run_binary("--xgl_property_seed")
        for result in (duplicate, environment, missing):
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("Invalid property seed", result.stderr)
            self.assertNotIn("[ RUN", result.stdout)

    def test_environment_seed_and_cli_precedence(self):
        environment = self.run_binary(seed="17")
        override = self.run_binary("--xgl_property_seed=19", seed="invalid")
        self.assertEqual(environment.returncode, 0, environment.stdout + environment.stderr)
        self.assertIn("XGL_PROPERTY_SEED=17", environment.stdout)
        self.assertEqual(override.returncode, 0, override.stdout + override.stderr)
        self.assertIn("XGL_PROPERTY_SEED=19", override.stdout)

    def test_invalid_seed_fails_before_running_tests(self):
        for seed in ("", "-1", "4294967296", "1x"):
            with self.subTest(seed=seed):
                result = self.run_binary("--xgl_property_seed=" + seed)
                self.assertNotEqual(result.returncode, 0, result.stdout)
                self.assertIn("Invalid property seed", result.stderr)
                self.assertNotIn("[ RUN", result.stdout)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", required=True, type=Path)
    parser.add_argument("--binary", required=True, type=Path)
    parser.add_argument("--config", default="Debug")
    OPTIONS, arguments = parser.parse_known_args()
    OPTIONS.build_dir = OPTIONS.build_dir.resolve()
    OPTIONS.binary = OPTIONS.binary.resolve()
    unittest.main(argv=[sys.argv[0], *arguments])
