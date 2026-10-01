"""Exercise supported profiles and multi-configuration SDK consumers."""

import argparse
import json
from pathlib import Path
import subprocess
import tempfile
import unittest


PACKAGES = ("status", "bytes", "crc", "memory", "containers")
ARGS = None


class ProfileBuilds(unittest.TestCase):
    """Build real protocol targets using explicitly prepared dependencies."""

    @classmethod
    def setUpClass(cls):
        ARGS.work_dir.mkdir(parents=True, exist_ok=True)
        cls.root = Path(tempfile.mkdtemp(prefix="profiles-", dir=ARGS.work_dir))
        cls.commands = []

    @classmethod
    def tearDownClass(cls):
        (cls.root / "commands.json").write_text(
            json.dumps(cls.commands, indent=2) + "\n", encoding="utf-8")
        print(f"Profile evidence: {cls.root}")

    def execute(self, *command):
        result = subprocess.run([str(part) for part in command],
                                capture_output=True, text=True, encoding="utf-8",
                                errors="replace", timeout=300, check=False)
        log = self.root / f"command-{len(self.commands):03d}.log"
        log.write_text(result.stdout + result.stderr, encoding="utf-8")
        self.commands.append({"arguments": [str(p) for p in command],
                              "exit_code": result.returncode, "log": str(log)})
        self.assertEqual(result.returncode, 0, f"{log}\n{result.stdout}{result.stderr}")
        return result

    def configure(self, name, profile, *, tests=False, multi=False):
        binary = self.root / name
        command = ["cmake", "-S", ARGS.link_source / "dev", "-B", binary,
                   "-G", "Ninja Multi-Config" if multi else "Ninja",
                   f"-DXGL_PROFILE={profile}", "-DCMAKE_BUILD_TYPE=Debug",
                   f"-DXGL_BUILD_TESTS={'ON' if tests else 'OFF'}",
                   "-DXGL_BUILD_EXAMPLES=OFF", "-DXGL_BUILD_DOCS=OFF",
                   "-DXGL_BUILD_STATIC_ANALYSIS_TARGET=OFF",
                   "-DXGL_BUILD_FOOTPRINT_REPORT=OFF",
                   "-DXGL_BUILD_NOHEAP_SMOKE=ON",
                   "-DXGL_BUILD_SDK_CONSUMER_SMOKE=ON",
                   "-DXGL_BUILD_RELEASE_VALIDATION_TARGET=ON",
                   "-DXGL_ALLOW_FALLBACK_MALLOC=OFF"]
        for package in PACKAGES:
            command.append(f"-DXGL_DEV_{package.upper()}_SOURCE_DIR="
                           f"{getattr(ARGS, package + '_source').as_posix()}")
        if ARGS.gtest_dir:
            command.append(f"-DGTest_DIR={ARGS.gtest_dir.as_posix()}")
        if ARGS.c_compiler:
            command.append(f"-DCMAKE_C_COMPILER={ARGS.c_compiler}")
        if ARGS.cxx_compiler:
            command.append(f"-DCMAKE_CXX_COMPILER={ARGS.cxx_compiler}")
        self.execute(*command)
        return binary

    def test_multiconfig_release_and_installed_consumer(self):
        binary = self.configure("multi", "boot", multi=True)
        for configuration in ("Debug", "Release"):
            with self.subTest(configuration=configuration):
                self.execute("cmake", "--build", binary, "--config", configuration,
                             "--target", "xgl_release_validation", "--parallel", "2")

    def test_boot_runs_the_supported_capability_suite(self):
        binary = self.configure("boot", "boot", tests=True)
        self.execute("cmake", "--build", binary, "--parallel", "2")
        listing = json.loads(self.execute("ctest", "--test-dir", binary,
                                         "--show-only=json-v1").stdout)
        names = {test["name"] for test in listing["tests"]}
        self.assertIn("XglBoundedProfileTest.ReliableDeliveryUsesExplicitStorage", names)
        self.assertIn("XglBoundedProfileTest.RejectsUnsupportedAuthentication", names)
        self.execute("ctest", "--test-dir", binary, "--output-on-failure",
                     "--no-tests=error")

    def test_embedded_lite_has_an_explicit_capability_identity(self):
        binary = self.configure("lite", "embedded-lite", tests=True)
        self.execute("cmake", "--build", binary, "--parallel", "2")
        self.execute("ctest", "--test-dir", binary, "--output-on-failure",
                     "--no-tests=error")
        config = (binary / "link/generated/xgl/xgl_build_config.h").read_text()
        for definition in ("XGL_BUILD_CONFIG_ID 304U", "XGL_FEATURE_AUTH 0",
                           "XGL_FEATURE_FORWARDING 0", "XGL_FEATURE_FRAGMENTATION 1",
                           "XGL_FEATURE_OUT_OF_ORDER 1"):
            self.assertIn("#define " + definition, config)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--link-source", type=Path, required=True)
    parser.add_argument("--work-dir", type=Path, required=True)
    parser.add_argument("--gtest-dir", type=Path)
    parser.add_argument("--c-compiler")
    parser.add_argument("--cxx-compiler")
    for package in PACKAGES:
        parser.add_argument(f"--{package}-source", type=Path, required=True)
    ARGS, remaining = parser.parse_known_args()
    for name, value in vars(ARGS).items():
        if isinstance(value, Path):
            setattr(ARGS, name, value.resolve())
    unittest.main(argv=[__file__, *remaining])
