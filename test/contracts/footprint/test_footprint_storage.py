"""Keep target layout and budget contracts explicit after inline window storage."""

import importlib.util
import json
from pathlib import Path
import struct
import tempfile
from types import SimpleNamespace
import unittest


ROOT = Path(__file__).resolve().parents[3]


def load_tool(name):
    spec = importlib.util.spec_from_file_location(
        name, ROOT / "tools/boot_footprint" / f"{name}.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class FootprintStorage(unittest.TestCase):
    """Exercise the production decoder and audit using fixed target fixtures."""

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="xgl-footprint-storage-")
        self.directory = Path(self.temporary.name)
        self.report = load_tool("report")
        self.budget = load_tool("check_budget")

    def tearDown(self):
        self.temporary.cleanup()

    def decode(self, values):
        source = self.directory / "layout.bin"
        source.write_bytes(struct.pack("<" + "I" * len(values), *values))
        output = self.directory / "layout.json"
        self.report.generate_layout(SimpleNamespace(
            input=source, output=output, header=self.directory / "layout.h"))
        return json.loads(output.read_text())

    def fixture(self):
        # Target ABI fixture; this is not a claim about the current ELF size.
        return [0x58474C42, 7, 4, 8, 512, 20, 40, 104, 72, 204, 20, 76,
                128, 128, 1, 1, 1, 1, 1, 1, 1, 24, 2, 32, 0, 67081, 1, 1, 0, 5]

    def test_inline_bitmap_does_not_create_an_independent_runtime_block(self):
        layout = self.decode(self.fixture())
        self.assertEqual(layout["format_version"], 7)
        self.assertEqual(layout["window_pool_blocks"], 0)
        self.assertEqual(layout["runtime_partitions"]["window"],
                         {"block_bytes": 0, "blocks": 0, "bytes": 0})
        self.assertEqual(layout["runtime_blocks"], 4)
        self.assertEqual(layout["workspace_bytes"], 1112)

    def test_old_layout_cannot_silently_reintroduce_the_window_pool(self):
        values = self.fixture()[:-1]
        values[1] = 4
        with self.assertRaises(ValueError):
            self.decode(values)

    def test_current_layout_rejects_external_window_pool(self):
        values = self.fixture()
        values[24] = 1
        with self.assertRaises(ValueError):
            self.decode(values)

    def test_layout_records_resolved_transport_resource_identity(self):
        values = self.fixture()
        layout = self.decode(values)
        self.assertEqual(layout["resource_config_id"], 67081)
        self.assertEqual(layout["compiled_window_capacity"], 1)
        self.assertEqual(layout["reliable_bucket_count"], 1)
        self.assertEqual(layout["adaptive_rto"], 0)
        self.assertEqual(layout["config_abi_version"], 5)

    def test_budget_rejects_different_transport_resources(self):
        policy = {"schema_version": 1, "compiler_version": "15.2.1",
                  "configuration": {"profile": "boot", "resource_config_id": 1025,
                                    "compiled_window_capacity": 1,
                                    "reliable_bucket_count": 1, "adaptive_rto": False},
                  "baseline": {"flash_bytes": 16168},
                  "limits": {"flash_bytes": 16384}}
        report = {"compiler": "arm-none-eabi-gcc 15.2.1",
                  "build_parameters": {"XGL_PROFILE": "boot"}, "flash_bytes": 15000,
                  "resource_config_id": 4097, "compiled_window_capacity": 128,
                  "reliable_bucket_count": 32, "adaptive_rto": True,
                  "forbidden_heap_symbols": [], "unresolved_symbols": []}
        result = self.budget.audit(report, policy)
        self.assertEqual(result["status"], "failed")
        self.assertEqual(len(result["violations"]), 4)
        report.update(resource_config_id=1025, compiled_window_capacity=1,
                      reliable_bucket_count=1, adaptive_rto=False)
        self.assertEqual(self.budget.audit(report, policy)["status"], "passed")

    def test_budget_rejects_missing_or_nonzero_window_pool_blocks(self):
        policy = {"schema_version": 1, "compiler_version": "15.2.1",
                  "configuration": {"profile": "boot", "window_pool_blocks": 0},
                  "baseline": {"flash_bytes": 16168},
                  "limits": {"flash_bytes": 16384}}
        report = {"compiler": "arm-none-eabi-gcc 15.2.1",
                  "build_parameters": {"XGL_PROFILE": "boot"}, "flash_bytes": 15000,
                  "forbidden_heap_symbols": [], "unresolved_symbols": []}
        for value in (None, 1, False, "0"):
            with self.subTest(window_pool_blocks=value):
                report["window_pool_blocks"] = value
                audit = self.budget.audit(report, policy)
                self.assertEqual(audit["status"], "failed")
                self.assertTrue(any("window_pool_blocks" in v for v in audit["violations"]))
        report["window_pool_blocks"] = 0
        self.assertEqual(self.budget.audit(report, policy)["status"], "passed")


if __name__ == "__main__":
    unittest.main()
