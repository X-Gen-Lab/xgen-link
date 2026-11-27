"""Verify the evidence written by the strict runner using a real CTest registry."""

import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET


RUNNER = Path(__file__).resolve().parents[2] / "cmake" / "run_ctest.py"
PYTHON = Path(sys.executable).as_posix()


class CTestEvidenceTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="xgl-ctest-evidence-")
        self.addCleanup(temporary.cleanup)
        self.build = Path(temporary.name)
        self.registry = self.build / "CTestTestfile.cmake"
        self.inventory = self.build / "ctest-inventory.json"
        self.junit = self.build / "ctest.xml"
        self.inventory.write_text("stale inventory", encoding="utf-8")
        self.junit.write_text("stale JUnit", encoding="utf-8")

    def run_fixture(self, registry):
        self.registry.write_text(registry, encoding="utf-8")
        return subprocess.run(
            [sys.executable, RUNNER, "--build-dir", self.build, "--config", "Debug"],
            capture_output=True, text=True, check=False, timeout=30,
        )

    def test_discovery_failure_removes_previous_evidence(self):
        result = self.run_fixture('message(FATAL_ERROR "fixture discovery failure")\n')
        self.assertIn("fixture discovery failure", result.stderr)
        self.assertFalse(self.inventory.exists())
        self.assertFalse(self.junit.exists())
        discovery = subprocess.run(
            ["ctest", "--test-dir", self.build, "-C", "Debug", "--show-only=json-v1"],
            capture_output=True, check=False, timeout=30,
        )
        self.assertNotEqual(discovery.returncode, 0)
        self.assertEqual(result.returncode, discovery.returncode)

    def test_empty_inventory_is_rejected_without_previous_junit(self):
        result = self.run_fixture("")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Expected a nonempty inventory", result.stderr)
        self.assertEqual(json.loads(self.inventory.read_bytes())["tests"], [])
        self.assertFalse(self.junit.exists())

    def test_skipped_case_is_rejected_with_current_junit(self):
        result = self.run_fixture(
            f'add_test("skipped" "{PYTHON}" -c "raise SystemExit(1)")\n'
            'set_tests_properties("skipped" PROPERTIES SKIP_RETURN_CODE 1)\n'
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("CTest case did not pass: skipped", result.stderr)
        self.assertEqual(json.loads(self.inventory.read_bytes())["tests"][0]["name"],
                         "skipped")
        self.assertIsNotNone(ET.parse(self.junit).find(".//testcase/skipped"))

    def test_success_records_matching_inventory_and_junit(self):
        result = self.run_fixture(f'add_test("fresh" "{PYTHON}" -c "pass")\n')
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("Verified 1 CTest executions", result.stdout)
        self.assertEqual(json.loads(self.inventory.read_bytes())["tests"][0]["name"],
                         "fresh")
        case = ET.parse(self.junit).find(".//testcase")
        self.assertEqual(case.get("name"), "fresh")
        self.assertEqual(case.get("status"), "run")

    def test_multi_config_reports_survive_sequential_runs(self):
        source = self.build / "source"
        source.mkdir()
        (source / "CMakeLists.txt").write_text(
            'cmake_minimum_required(VERSION 3.24)\nproject(ctest_fixture NONE)\n'
            'enable_testing()\n'
            'add_test(NAME check_config COMMAND "${CMAKE_COMMAND}" -E echo "$<CONFIG>")\n',
            encoding="utf-8",
        )
        binary = self.build / "multi"
        subprocess.run(
            ["cmake", "-S", source, "-B", binary, "-G", "Ninja Multi-Config"],
            check=True, capture_output=True, timeout=30,
        )
        for configuration in ("Debug", "Release"):
            subprocess.run(
                [sys.executable, RUNNER, "--build-dir", binary, "--config", configuration],
                check=True, capture_output=True, timeout=30,
            )
        for configuration in ("Debug", "Release"):
            with self.subTest(configuration=configuration):
                directory = binary / "test-results" / configuration
                inventory = json.loads((directory / "ctest-inventory.json").read_bytes())
                self.assertEqual(inventory["tests"][0]["command"][-1], configuration)
                case = ET.parse(directory / "ctest.xml").find(".//testcase/system-out")
                self.assertEqual(case.text.strip(), configuration)


if __name__ == "__main__":
    unittest.main()
