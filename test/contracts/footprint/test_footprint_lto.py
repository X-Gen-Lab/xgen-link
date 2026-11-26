"""Exercise explicit ARM LTO builds without specializing the external probe."""

import argparse
import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile
import unittest


ARGS = None


class FootprintLto(unittest.TestCase):
    """Inspect actual build commands, final reports and budget separation."""

    @classmethod
    def setUpClass(cls):
        ARGS.work_dir.mkdir(parents=True, exist_ok=True)
        cls.root = Path(tempfile.mkdtemp(prefix="footprint-lto-", dir=ARGS.work_dir))
        cls.commands = []
        spec = importlib.util.spec_from_file_location(
            "check_budget", ARGS.link_source / "tools/boot_footprint/check_budget.py")
        cls.budget = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(cls.budget)

    @classmethod
    def tearDownClass(cls):
        (cls.root / "commands.json").write_text(
            json.dumps(cls.commands, indent=2) + "\n", encoding="utf-8")
        print(f"Footprint LTO evidence: {cls.root}")

    def execute(self, *command, expected_success=True):
        result = subprocess.run([str(part) for part in command],
                                capture_output=True, text=True, encoding="utf-8",
                                errors="replace", timeout=300, check=False)
        log = self.root / f"command-{len(self.commands):03d}.log"
        log.write_text(result.stdout + result.stderr, encoding="utf-8")
        self.commands.append({"arguments": [str(part) for part in command],
                              "exit_code": result.returncode, "log": str(log)})
        self.assertEqual(result.returncode == 0, expected_success,
                         f"{log}\n{result.stdout}{result.stderr}")
        return result.stdout

    def configure(self, name, enabled):
        binary = self.root / name
        source = ARGS.link_source / "tools/boot_footprint"
        command = ["cmake", "-S", source, "-B", binary, "-G", "Ninja",
                   f"-DCMAKE_TOOLCHAIN_FILE={source / 'toolchain-arm-cortex-m0.cmake'}",
                   "-DCMAKE_BUILD_TYPE=MinSizeRel", "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"]
        if enabled:
            command.append("-DXGL_FOOTPRINT_LTO=ON")
        self.execute(*command)
        return binary

    def verify_build(self, enabled):
        binary = ARGS.build_dir
        if binary is None:
            binary = self.configure("enabled" if enabled else "default", enabled)
        cache = (binary / "CMakeCache.txt").read_text(encoding="utf-8")
        source = ARGS.link_source / "tools/boot_footprint"
        self.assertIn(f"CMAKE_HOME_DIRECTORY:INTERNAL={source.as_posix()}", cache)
        self.assertIn(f"XGL_FOOTPRINT_LTO:BOOL={'ON' if enabled else 'OFF'}", cache)
        commands = json.loads((binary / "compile_commands.json").read_text())
        production = [item for item in commands
                      if "/src/" in Path(item["file"]).as_posix()]
        self.assertGreater(len(production), 10)
        for item in production:
            with self.subTest(source=item["file"]):
                self.assertEqual("-flto" in item["command"], enabled)
        for name in ("layout_probe.c", "consumer.c"):
            command = next(item["command"] for item in commands
                           if Path(item["file"]).name == name)
            self.assertNotIn("-flto", command)
        ninja = self.execute("ninja", "-C", binary, "-t", "commands", "xgl_boot_probe")
        link = next(line for line in reversed(ninja.splitlines()) if " -o xgl_boot_probe.elf " in line)
        self.assertEqual("-flto" in link, enabled)
        if ARGS.build_dir is None:
            self.execute("cmake", "--build", binary, "--parallel", "4")
        elf = binary / "xgl_boot_probe.elf"
        self.assertTrue(elf.is_file())
        report_json = binary / "boot-footprint.json"
        report_text = binary / "boot-footprint.txt"
        report = json.loads(report_json.read_text())
        self.assertIs(report["lto"], enabled)
        self.assertIs(report["optimization"]["library_ipo"], enabled)
        self.assertIs(report["optimization"]["layout_ipo"], False)
        self.assertIs(report["optimization"]["consumer_ipo"], False)
        self.assertEqual(bool(report["optimization"]["compile_options"]), enabled)
        self.assertEqual(bool(report["optimization"]["link_options"]), enabled)
        self.assertEqual(report["forbidden_heap_symbols"], [])
        self.assertEqual(report["unresolved_symbols"], [])
        self.assertGreater(report["flash_bytes"], 0)
        if ARGS.build_dir is None:
            self.verify_report_rebuild(binary, elf, report_json, report_text)

    def verify_report_rebuild(self, binary, elf, report_json, report_text):
        elf_mtime = elf.stat().st_mtime_ns
        build = ("cmake", "--build", binary, "--target", "xgl_boot_probe")
        for report in (report_json, report_text):
            report.unlink()
            self.execute(*build)
            self.assertTrue(report.is_file())
            self.assertEqual(elf.stat().st_mtime_ns, elf_mtime)

        invalid_stack = binary / "invalid-linked-stack.su"
        invalid_stack.write_text("probe.c:1:1:Reset_Handler\t8\tdynamic\n", encoding="utf-8")
        try:
            report_json.unlink()
            for _ in range(2):
                self.execute(*build, expected_success=False)
                rejected = json.loads(report_json.read_text())
                self.assertTrue(rejected["stack_evidence_errors"])
        finally:
            invalid_stack.unlink()
        self.execute(*build)
        self.assertFalse(json.loads(report_json.read_text())["stack_evidence_errors"])
        self.assertEqual(elf.stat().st_mtime_ns, elf_mtime)

    def test_default_keeps_every_object_and_link_without_lto(self):
        self.verify_build(False)

    def test_lto_preserves_opaque_layout_and_external_consumer(self):
        self.verify_build(True)

    def fixture(self):
        policy = {"schema_version": 1, "compiler_version": "15.2.1",
                  "configuration": {"profile": "boot", "lto": False},
                  "baseline": {"flash_bytes": 16060},
                  "limits": {"flash_bytes": 16384}}
        report = {"compiler": "arm-none-eabi-gcc 15.2.1",
                  "build_parameters": {"XGL_PROFILE": "boot"},
                  "flash_bytes": 12000, "lto": False,
                  "forbidden_heap_symbols": [], "unresolved_symbols": []}
        return policy, report

    def test_non_lto_policy_rejects_enabled_missing_and_nonboolean_mode(self):
        policy, report = self.fixture()
        for value in (True, None, 0, "OFF"):
            with self.subTest(lto=value):
                if value is None:
                    report.pop("lto", None)
                else:
                    report["lto"] = value
                audit = self.budget.audit(report, policy)
                self.assertEqual(audit["status"], "failed")
                self.assertTrue(any("lto" in item for item in audit["violations"]))

    def test_policy_accepts_only_its_explicit_optimization_mode(self):
        policy, report = self.fixture()
        for enabled in (False, True):
            with self.subTest(lto=enabled):
                policy["configuration"]["lto"] = enabled
                report["lto"] = enabled
                self.assertEqual(self.budget.audit(report, policy)["status"], "passed")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--link-source", type=Path, required=True)
    parser.add_argument("--work-dir", type=Path, required=True)
    parser.add_argument("--build-dir", type=Path,
                        help="Inspect an existing Ninja probe build with compile commands")
    parser.add_argument("--expect-lto", choices=("OFF", "ON"),
                        help="Expected mode of the existing build")
    ARGS, remaining = parser.parse_known_args()
    if (ARGS.build_dir is None) != (ARGS.expect_lto is None):
        parser.error("--build-dir and --expect-lto must be provided together")
    for name, value in vars(ARGS).items():
        if isinstance(value, Path):
            setattr(ARGS, name, value.resolve())
    selected = None
    if ARGS.expect_lto == "OFF":
        selected = [
            "FootprintLto.test_default_keeps_every_object_and_link_without_lto",
            "FootprintLto.test_non_lto_policy_rejects_enabled_missing_and_nonboolean_mode",
            "FootprintLto.test_policy_accepts_only_its_explicit_optimization_mode",
        ]
    elif ARGS.expect_lto == "ON":
        selected = ["FootprintLto.test_lto_preserves_opaque_layout_and_external_consumer"]
    unittest.main(argv=[__file__, *remaining], defaultTest=selected)
