"""Contracts for the explicit, offline development assembly."""

import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


DEV = Path(__file__).resolve().parents[1]


class DevelopmentEntryTests(unittest.TestCase):
    def test_missing_sources_fail_with_the_explicit_development_variable(self):
        with tempfile.TemporaryDirectory() as directory:
            result = subprocess.run(
                ["cmake", "-S", str(DEV), "-B", directory, "-G", "Ninja"],
                capture_output=True, text=True, check=False)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("XGL_DEV_STATUS_SOURCE_DIR", result.stdout + result.stderr)

    def test_repository_source_must_be_explicit_before_ci_checkout(self):
        environment = dict(os.environ)
        for name in ("STATUS", "BYTES", "CRC", "MEMORY", "CONTAINERS"):
            environment.pop("XGEN_" + name + "_REPOSITORY", None)
        result = subprocess.run(
            [sys.executable, str(DEV / "prepare_dependencies.py"),
             "--manifest", str(DEV / "dependencies.json")],
            env=environment, capture_output=True, text=True, check=False)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("XGEN_STATUS_REPOSITORY", result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
