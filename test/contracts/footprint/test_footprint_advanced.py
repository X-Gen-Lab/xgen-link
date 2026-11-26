"""Distinguish an enabled library from a linked advanced API Boot consumer."""

import argparse
import json
from pathlib import Path
import subprocess
import tempfile
import unittest


ARGS = None


class FootprintAdvanced(unittest.TestCase):
    """Use actual Cortex-M0 ELF symbols and reports, without target execution."""

    @classmethod
    def setUpClass(cls):
        ARGS.work_dir.mkdir(parents=True, exist_ok=True)
        cls.root = Path(tempfile.mkdtemp(prefix="footprint-advanced-", dir=ARGS.work_dir))
        cls.commands = []

    @classmethod
    def tearDownClass(cls):
        (cls.root / "commands.json").write_text(
            json.dumps(cls.commands, indent=2) + "\n", encoding="utf-8")
        print(f"Advanced Boot probe evidence: {cls.root}")

    def execute(self, *command):
        result = subprocess.run([str(part) for part in command], capture_output=True,
                                text=True, encoding="utf-8", errors="replace",
                                timeout=300, check=False)
        log = self.root / f"command-{len(self.commands):03d}.log"
        log.write_text(result.stdout + result.stderr, encoding="utf-8")
        self.commands.append({"arguments": [str(part) for part in command],
                              "exit_code": result.returncode, "log": str(log)})
        self.assertEqual(result.returncode, 0, f"{log}\n{result.stdout}{result.stderr}")
        return result.stdout

    def verify_probe(self, advanced_api):
        source = ARGS.link_source / "tools/boot_footprint"
        binary = self.root / ("advanced-api" if advanced_api else "legacy-api")
        command = ["cmake", "-S", source, "-B", binary, "-G", "Ninja",
                   f"-DCMAKE_TOOLCHAIN_FILE={source / 'toolchain-arm-cortex-m0.cmake'}",
                   "-DCMAKE_BUILD_TYPE=MinSizeRel", "-DXGL_FOOTPRINT_LTO=ON",
                   "-DXGL_ENABLE_ADVANCED_RUNTIME=ON",
                   f"-DXGL_FOOTPRINT_ADVANCED_API={'ON' if advanced_api else 'OFF'}"]
        self.execute(*command)
        self.execute("cmake", "--build", binary, "--parallel", "4")
        report = json.loads((binary / "boot-footprint.json").read_text(encoding="utf-8"))
        self.assertIs(report["advanced_runtime"], True)
        self.assertIs(report["advanced_api_probe"], advanced_api)
        self.assertEqual(report["build_parameters"]["XGL_ENABLE_ADVANCED_RUNTIME"], "ON")
        self.assertEqual(report["build_parameters"]["XGL_FOOTPRINT_ADVANCED_API"],
                         "ON" if advanced_api else "OFF")
        self.assertEqual(report["forbidden_heap_symbols"], [])
        self.assertEqual(report["unresolved_symbols"], [])
        symbols = self.execute("arm-none-eabi-nm", "--defined-only", binary / "xgl_boot_probe.elf")
        for symbol in ("xgl_init_static_with_options", "xgl_memory_report_with_options",
                       "xgl_step_bounded", "xgl_resources_get"):
            self.assertEqual(symbol in symbols, advanced_api, symbol)

    def test_enabled_library_does_not_claim_advanced_api_coverage(self):
        self.verify_probe(False)

    def test_explicit_probe_links_advanced_public_api_paths(self):
        self.verify_probe(True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--link-source", type=Path, required=True)
    parser.add_argument("--work-dir", type=Path, required=True)
    ARGS, remaining = parser.parse_known_args()
    for name, value in vars(ARGS).items():
        if isinstance(value, Path):
            setattr(ARGS, name, value.resolve())
    unittest.main(argv=[__file__, *remaining])
