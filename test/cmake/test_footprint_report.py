"""Reject incomplete stack evidence and track the probe's build inputs."""

import importlib.util
import json
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "footprint_report", ROOT / "tools/boot_footprint/report.py")
REPORT = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(REPORT)


class FootprintReport(unittest.TestCase):
    """Use fixed external tool output while exercising the real report writer."""

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="xgl-elf-report-")
        self.directory = Path(self.temporary.name)
        self.output = self.directory / "report.json"
        self.layout = self.directory / "layout.json"
        self.layout.write_text(json.dumps({
            "frame_bytes": 128, "rx_bytes": 128, "rx_chunk_bytes": 32,
            "workspace_bytes": 1072, "format_version": 6,
            "window_pool_blocks": 0, "resource_config_id": 1857,
            "compiled_window_capacity": 128, "reliable_bucket_count": 1,
            "adaptive_rto": 1,
        }), encoding="utf-8")
        (self.directory / "CMakeCache.txt").write_text(
            "XGL_PROFILE:STRING=boot\n", encoding="utf-8")
        (self.directory / "footprint-optimization.json").write_text(
            '{"enabled": false}\n', encoding="utf-8")

    def tearDown(self):
        self.temporary.cleanup()

    def run_report(self, stack=None):
        if stack is not None:
            (self.directory / "probe.su").write_text(stack, encoding="utf-8")
        args = SimpleNamespace(
            elf=self.directory / "probe.elf", build=self.directory,
            layout=self.layout, size="size", nm="nm", compiler="gcc",
            configuration="MinSizeRel", output=self.output)
        tool_output = {
            ("size", "-B"): "text data bss dec hex filename\n1000 0 2048 3048 be8 probe.elf\n",
            ("size", "-A"): ".text 1000 0\n.bss 1024 0\n.stack 1024 0\n",
            ("nm", "--defined-only"): "00000001 T main\n00000011 t worker\n",
            ("nm", "--undefined-only"): "",
            ("gcc", "--version"): "arm-none-eabi-gcc 15.2.1\n",
        }
        with patch.object(REPORT, "run", side_effect=lambda args: tool_output[tuple(args[:2])]):
            with patch.object(REPORT, "source_identity", return_value={"revision": "fixture"}):
                return REPORT.report_elf(args)

    def test_missing_stack_records_fail_the_report_step(self):
        self.assertNotEqual(self.run_report(), 0)

    def test_records_for_unlinked_functions_do_not_satisfy_stack_evidence(self):
        self.assertNotEqual(self.run_report("probe.c:1:1:removed\t4096\tstatic\n"), 0)

    def test_unbounded_dynamic_stack_fails_even_with_small_numeric_size(self):
        self.assertNotEqual(self.run_report("probe.c:1:1:main\t8\tdynamic\n"), 0)

    def test_malformed_stack_record_cannot_hide_behind_a_valid_record(self):
        self.assertNotEqual(self.run_report(
            "probe.c:1:1:main\t8\tstatic\nprobe.c:2:1:worker\tunknown\tstatic\n"), 0)

    def test_partial_stack_evidence_does_not_publish_a_numeric_maximum(self):
        self.run_report(
            "probe.c:1:1:main\t8\tstatic\nprobe.c:2:1:worker\t16\tdynamic\n")
        result = json.loads(self.output.read_text(encoding="utf-8"))
        self.assertIsNone(result["largest_linked_function_stack_bytes"])

    def test_static_and_bounded_dynamic_stack_have_a_finite_report(self):
        self.assertEqual(self.run_report(
            "probe.c:1:1:main\t0\tstatic\nprobe.c:2:1:worker\t24\tdynamic,bounded\n"), 0)
        result = json.loads(self.output.read_text(encoding="utf-8"))
        self.assertEqual(result["largest_linked_function_stack_bytes"], 24)

    def test_unlinked_unbounded_stack_does_not_reject_the_linked_image(self):
        self.assertEqual(self.run_report(
            "probe.c:1:1:main\t8\tstatic\nprobe.c:2:1:removed\t8\tdynamic\n"), 0)

    def test_source_identity_includes_nested_probe_cmake(self):
        self.assert_build_input_tracked("tools/boot_footprint/CMakeLists.txt")

    def test_source_identity_includes_profile_configuration(self):
        self.assert_build_input_tracked("cmake/XglProfiles.cmake")

    def assert_build_input_tracked(self, relative):
        (self.directory / "CMakeLists.txt").write_text("project(probe)\n", encoding="utf-8")
        source = self.directory / relative
        source.parent.mkdir(parents=True, exist_ok=True)
        source.write_text("# initial build input\n", encoding="utf-8")
        before = REPORT.source_identity(self.directory)["source_sha256"]
        source.write_text("# changed build input\n", encoding="utf-8")
        after = REPORT.source_identity(self.directory)["source_sha256"]
        self.assertNotEqual(before, after)


if __name__ == "__main__":
    unittest.main()
