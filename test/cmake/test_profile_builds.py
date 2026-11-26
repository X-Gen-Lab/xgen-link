"""Exercise supported profiles and multi-configuration SDK consumers."""

import argparse
import json
from pathlib import Path
import subprocess
import tempfile
import unittest


ARGS = None


class ProfileBuilds(unittest.TestCase):
    """Build and exercise real protocol targets."""

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

    def execute(self, *command, expected_success=True):
        result = subprocess.run([str(part) for part in command],
                                capture_output=True, text=True, encoding="utf-8",
                                errors="replace", timeout=300, check=False)
        log = self.root / f"command-{len(self.commands):03d}.log"
        log.write_text(result.stdout + result.stderr, encoding="utf-8")
        self.commands.append({"arguments": [str(p) for p in command],
                              "exit_code": result.returncode, "log": str(log)})
        if expected_success:
            self.assertEqual(result.returncode, 0, f"{log}\n{result.stdout}{result.stderr}")
        else:
            self.assertNotEqual(result.returncode, 0, f"Unexpected acceptance: {log}")
        return result

    def configure(self, name, profile, *, tests=False, multi=False, examples=False):
        binary = self.root / name
        command = ["cmake", "-S", ARGS.link_source, "-B", binary,
                   "-G", "Ninja Multi-Config" if multi else "Ninja",
                   f"-DXGL_PROFILE={profile}", "-DCMAKE_BUILD_TYPE=Debug",
                   f"-DXGL_BUILD_TESTS={'ON' if tests else 'OFF'}",
                   f"-DXGL_BUILD_EXAMPLES={'ON' if examples else 'OFF'}",
                   "-DXGL_BUILD_DOCS=OFF",
                   "-DXGL_BUILD_STATIC_ANALYSIS_TARGET=OFF",
                   "-DXGL_BUILD_FOOTPRINT_REPORT=OFF",
                   "-DXGL_BUILD_NOHEAP_SMOKE=ON",
                   "-DXGL_BUILD_SDK_CONSUMER_SMOKE=ON",
                   "-DXGL_BUILD_RELEASE_VALIDATION_TARGET=ON",
                   "-DXGL_ALLOW_FALLBACK_MALLOC=OFF"]
        if ARGS.gtest_dir:
            command.append(f"-DGTest_DIR={ARGS.gtest_dir.as_posix()}")
        if ARGS.c_compiler:
            command.append(f"-DCMAKE_C_COMPILER={ARGS.c_compiler}")
        if ARGS.cxx_compiler:
            command.append(f"-DCMAKE_CXX_COMPILER={ARGS.cxx_compiler}")
        self.execute(*command)
        return binary

    def test_examples_follow_compiled_forwarding_capability(self):
        for profile, forwarding in (("boot", False), ("embedded-lite", False),
                                    ("embedded", True), ("full", True)):
            with self.subTest(profile=profile):
                binary = self.configure("examples-" + profile, profile, examples=True)
                self.execute("cmake", "--build", binary, "--parallel", "2")
                listing = json.loads(self.execute("ctest", "--test-dir", binary,
                                                 "--show-only=json-v1").stdout)
                names = {test["name"] for test in listing["tests"]}
                examples = {name for name in names if name.endswith("_example")}
                expected = {"xgl_echo_example", "xgl_file_transfer_example",
                            "xgl_boot_update_example"}
                if forwarding:
                    expected.add("xgl_multi_node_example")
                self.assertEqual(examples, expected)
                self.execute("ctest", "--test-dir", binary, "-R", "_example$",
                             "--output-on-failure", "--no-tests=error")

    def test_multiconfig_release_and_installed_consumer(self):
        binary = self.configure("multi", "boot", multi=True)
        for configuration in ("Debug", "Release"):
            with self.subTest(configuration=configuration):
                self.execute("cmake", "--build", binary, "--config", configuration,
                             "--target", "xgl_release_validation", "--parallel", "2")
                installed = (binary / "sdk-consumer-smoke-ctest" / configuration
                             / "install/lib/cmake/xgl")
                self.assertTrue((installed / f"xglTargets-{configuration.lower()}.cmake").is_file())

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
        config = (binary / "generated/xgl/xgl_build_config.h").read_text()
        for definition in ("XGL_BUILD_CONFIG_ID 304U", "XGL_FEATURE_AUTH 0",
                           "XGL_FEATURE_FORWARDING 0", "XGL_FEATURE_FRAGMENTATION 1",
                           "XGL_FEATURE_OUT_OF_ORDER 1"):
            self.assertIn("#define " + definition, config)

    def test_installed_lite_package_declares_and_checks_capabilities(self):
        binary = self.configure("lite-package", "embedded-lite")
        self.execute("cmake", "--build", binary, "--target", "xgl_sdk_consumer_smoke",
                     "--parallel", "2")
        prefix = binary / "sdk-consumer-smoke/Debug/install"
        source = self.root / "capability-consumer"
        source.mkdir()
        (source / "CMakeLists.txt").write_text(
            'cmake_minimum_required(VERSION 3.24)\nproject(capability_consumer C)\n'
            'find_package(xgl CONFIG REQUIRED COMPONENTS ${REQUESTED_CAPABILITIES})\n'
            'if(NOT xgl_PROFILE STREQUAL "embedded-lite" OR NOT xgl_BUILD_CONFIG_ID EQUAL 304)\n'
            '  message(FATAL_ERROR "Installed profile identity does not match")\nendif()\n'
            'get_target_property(profile xgl::xgl XGL_PROFILE)\n'
            'if(NOT profile STREQUAL xgl_PROFILE)\n'
            '  message(FATAL_ERROR "Target and package identities disagree")\nendif()\n',
            encoding="utf-8")
        command = ["cmake", "-S", source, "-G", "Ninja",
                   f"-DCMAKE_PREFIX_PATH={prefix.as_posix()}"]
        if ARGS.c_compiler:
            command.append(f"-DCMAKE_C_COMPILER={ARGS.c_compiler}")
        self.execute(*command, "-B", self.root / "capability-valid",
                     "-DREQUESTED_CAPABILITIES=fragmentation;out_of_order")
        rejected = self.execute(*command, "-B", self.root / "capability-invalid",
                                "-DREQUESTED_CAPABILITIES=authentication",
                                expected_success=False)
        self.assertIn("Unsupported xgl capability: authentication",
                      rejected.stdout + rejected.stderr)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--link-source", type=Path, required=True)
    parser.add_argument("--work-dir", type=Path, required=True)
    parser.add_argument("--gtest-dir", type=Path)
    parser.add_argument("--c-compiler")
    parser.add_argument("--cxx-compiler")
    ARGS, remaining = parser.parse_known_args()
    for name, value in vars(ARGS).items():
        if isinstance(value, Path):
            setattr(ARGS, name, value.resolve())
    unittest.main(argv=[__file__, *remaining])
