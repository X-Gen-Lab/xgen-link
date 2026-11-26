"""Exercise delivery presets through real CMake and CTest entry points."""

import argparse
import json
from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
ARGS = None


class DeliveryPresets(unittest.TestCase):
    """Keep production builds distinct from requested host regressions."""

    @classmethod
    def setUpClass(cls):
        ARGS.work_dir.mkdir(parents=True, exist_ok=True)
        cls.work = Path(tempfile.mkdtemp(prefix="run-", dir=ARGS.work_dir))
        cls.commands = []

    @classmethod
    def tearDownClass(cls):
        (cls.work / "commands.json").write_text(
            json.dumps(cls.commands, indent=2) + "\n", encoding="utf-8")

    def prepare(self, suffix):
        source = self.work / suffix
        source.mkdir()
        presets = json.loads((ROOT / "CMakePresets.json").read_text(encoding="utf-8"))
        for preset in presets["configurePresets"]:
            if "binaryDir" in preset:
                preset["binaryDir"] = "${sourceDir}/build/${presetName}"
        (source / "CMakePresets.json").write_text(
            json.dumps(presets, indent=2) + "\n", encoding="utf-8")
        (source / "CMakeLists.txt").write_text(
            "cmake_minimum_required(VERSION 3.24)\n"
            "project(delivery_preset_contract NONE)\n"
            "enable_testing()\n"
            "if(XGL_REQUIRE_HOST_TESTS)\n"
            "  if(NOT XGL_BUILD_TESTS OR NOT XGL_PROFILE STREQUAL XGL_REQUIRE_PROFILE)\n"
            "    message(FATAL_ERROR \"Host preset did not select protocol regressions\")\n"
            "  endif()\n"
            "endif()\n", encoding="utf-8")
        return source, presets

    def execute(self, source, *command):
        result = subprocess.run(command, cwd=source, capture_output=True,
                                text=True, encoding="utf-8", errors="replace",
                                check=False, timeout=30)
        log = self.work / f"command-{len(self.commands):03d}.log"
        log.write_text(result.stdout + result.stderr, encoding="utf-8")
        self.commands.append({"command": list(command), "cwd": str(source),
                              "exit_code": result.returncode, "log": str(log)})
        return result

    def test_test_presets_reject_an_empty_selection(self):
        source, presets = self.prepare("empty")
        for preset in presets["testPresets"]:
            with self.subTest(preset=preset["name"]):
                configured = self.execute(
                    source, "cmake", "--preset", preset["configurePreset"])
                self.assertEqual(configured.returncode, 0, configured.stderr)
                result = self.execute(source, "ctest", "--preset", preset["name"])
                self.assertNotEqual(result.returncode, 0,
                                    "An empty CTest selection was reported as success")
                self.assertIn("No tests were found", result.stdout + result.stderr)

    def test_bounded_host_presets_enable_protocol_regressions(self):
        source, presets = self.prepare("host")
        build_names = {preset["name"] for preset in presets["buildPresets"]}
        test_names = {preset["name"] for preset in presets["testPresets"]}
        for profile in ("boot", "embedded-lite", "embedded"):
            name = profile + "-host-test"
            with self.subTest(preset=name):
                result = self.execute(
                    source, "cmake", "--preset", name,
                    "-DXGL_REQUIRE_HOST_TESTS=ON", f"-DXGL_REQUIRE_PROFILE={profile}")
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertIn(name, build_names)
                self.assertIn(name, test_names)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--work-dir", type=Path, required=True)
    ARGS, remaining = parser.parse_known_args()
    unittest.main(argv=[__file__, *remaining])
