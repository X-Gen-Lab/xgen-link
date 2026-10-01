"""Validate bounded transport capacities and SDK identity using real builds."""

import argparse
import json
from pathlib import Path
import subprocess
import tempfile
import unittest


PACKAGES = ("status", "bytes", "crc", "memory", "containers")
ARGS = None


class TransportResourceConfig(unittest.TestCase):
    """Compile production windows and consume the configured protocol SDK."""

    @classmethod
    def setUpClass(cls):
        ARGS.work_dir.mkdir(parents=True, exist_ok=True)
        cls.root = Path(tempfile.mkdtemp(prefix="transport-resource-", dir=ARGS.work_dir))
        cls.commands = []

    @classmethod
    def tearDownClass(cls):
        (cls.root / "commands.json").write_text(
            json.dumps(cls.commands, indent=2) + "\n", encoding="utf-8")
        print(f"Transport resource evidence: {cls.root}")

    def execute(self, *command, succeeds=True):
        result = subprocess.run([str(part) for part in command], capture_output=True,
                                text=True, encoding="utf-8", errors="replace",
                                timeout=300, check=False)
        log = self.root / f"command-{len(self.commands):03d}.log"
        log.write_text(result.stdout + result.stderr, encoding="utf-8")
        self.commands.append({"arguments": [str(part) for part in command],
                              "exit_code": result.returncode, "log": str(log)})
        if succeeds:
            self.assertEqual(result.returncode, 0, f"{log}\n{result.stdout}{result.stderr}")
        else:
            self.assertNotEqual(result.returncode, 0, f"Unexpected acceptance: {log}")
        return result

    def configure(self, name, profile="full", *, buckets="AUTO", capacity="AUTO",
                  adaptive="ON", heap=None, chunk="AUTO", succeeds=True):
        binary = self.root / name
        command = ["cmake", "-S", ARGS.link_source / "dev", "-B", binary, "-G", "Ninja",
                   f"-DXGL_PROFILE={profile}", "-DCMAKE_BUILD_TYPE=Debug",
                   "-DXGL_BUILD_TESTS=OFF", "-DXGL_BUILD_EXAMPLES=OFF",
                   "-DXGL_BUILD_DOCS=OFF", "-DXGL_BUILD_FOOTPRINT_REPORT=OFF",
                   "-DXGL_BUILD_NOHEAP_SMOKE=OFF", "-DXGL_BUILD_SDK_CONSUMER_SMOKE=OFF",
                   "-DXGL_BUILD_RELEASE_VALIDATION_TARGET=OFF",
                   "-DXGL_BUILD_STATIC_ANALYSIS_TARGET=OFF",
                   f"-DXGL_RELIABLE_INDEX_BUCKETS={buckets}",
                   f"-DXGL_TRANSPORT_WINDOW_CAPACITY={capacity}",
                   f"-DXGL_ENABLE_ADAPTIVE_RTO={adaptive}",
                   f"-DXGL_DATALINK_RX_CHUNK_SIZE={chunk}"]
        if heap is not None:
            command.append(f"-DXGL_ALLOW_FALLBACK_MALLOC={heap}")
        if ARGS.c_compiler:
            command.append(f"-DCMAKE_C_COMPILER={ARGS.c_compiler}")
        for package in PACKAGES:
            command.append(f"-DXGL_DEV_{package.upper()}_SOURCE_DIR="
                           f"{getattr(ARGS, package + '_source').as_posix()}")
        self.execute(*command, succeeds=succeeds)
        return binary

    def header(self, binary):
        return binary / "link/generated/xgl/xgl_build_config.h"

    def probe(self, name, binary, code, *, definitions="", window=False, succeeds=True):
        source = self.root / (name + "-probe")
        build = source / "build"
        source.mkdir(parents=True, exist_ok=True)
        (source / "main.c").write_text(code, encoding="utf-8")
        sources = (f' "{(ARGS.link_source / "src/transport/xgl_window.c").as_posix()}"'
                   if window else "")
        (source / "CMakeLists.txt").write_text(
            'cmake_minimum_required(VERSION 3.24)\nproject(transport_resource_probe C)\n'
            f'add_executable(probe main.c{sources})\n'
            'target_compile_features(probe PRIVATE c_std_11)\n'
            'target_include_directories(probe PRIVATE\n' +
            ''.join(f'    "{path.as_posix()}"\n' for path in (
                ARGS.link_source / "src", ARGS.link_source / "include",
                binary / "link/generated", ARGS.status_source / "include",
                ARGS.containers_source / "include")) + ')\n' +
            (f'target_compile_definitions(probe PRIVATE {definitions})\n' if definitions else "") +
            (f'target_sources(probe PRIVATE "{(ARGS.containers_source / "src/bitset.c").as_posix()}")\n'
             if window else ""), encoding="utf-8")
        command = ["cmake", "-S", source, "-B", build, "-G", "Ninja"]
        if ARGS.c_compiler:
            command.append(f"-DCMAKE_C_COMPILER={ARGS.c_compiler}")
        self.execute(*command)
        self.execute("cmake", "--build", build, succeeds=succeeds)
        if succeeds:
            executable = build / ("probe.exe" if (build / "probe.exe").is_file() else "probe")
            self.execute(executable)

    def test_default_profiles_keep_private_window_and_legacy_identity(self):
        for profile, config_id, buckets in (("boot", 301, 1), ("embedded", 302, 32),
                                            ("full", 303, 32), ("embedded-lite", 304, 8)):
            with self.subTest(profile=profile):
                binary = self.configure("default-" + profile, profile)
                self.probe("default-" + profile, binary,
                           '#include <xgl/xgl_build_config.h>\n'
                           f'_Static_assert(XGL_BUILD_CONFIG_ID == {config_id}U, "profile identity");\n'
                           f'_Static_assert(XGL_RELIABLE_BUCKET_COUNT == {buckets}U, "bucket capacity");\n'
                           '_Static_assert(XGL_BUILD_WINDOW_CAPACITY == 128U, "private window contract");\n'
                           '_Static_assert(XGL_FEATURE_ADAPTIVE_RTO == 1, "adaptive default");\n'
                           'int main(void) { return 0; }\n')

    def test_configuration_rejects_unsupported_capacities(self):
        for option, values in (("buckets", ("0", "3", "64", "invalid")),
                               ("capacity", ("0", "2", "256", "invalid")),
                               ("adaptive", ("invalid",))):
            for value in values:
                with self.subTest(option=option, value=value):
                    rejected = self.configure(f"invalid-{option}-{value}",
                                              **{option: value}, succeeds=False)
                    self.assertFalse(self.header(rejected).is_file())

    def test_explicit_window_enforces_boundary_and_preserves_failed_object(self):
        for capacity in (1, 8, 16, 32, 64, 128):
            with self.subTest(capacity=capacity):
                binary = self.configure(f"window-{capacity}", capacity=str(capacity))
                self.probe(f"window-{capacity}", binary,
                           '#include <string.h>\n#include "transport/xgl_window.h"\n'
                           f'_Static_assert(sizeof(((xgl_sliding_window_t*)0)->ack_received) == '
                           f'{(capacity + 7) // 8}U, "bounded bitmap");\n'
                           'int main(void) {\n'
                           '    xgl_sliding_window_t window, before;\n'
                           '    memset(&window, 0xA5, sizeof(window)); before = window;\n'
                           f'    if (xgl_window_init(&window, {capacity + 1}U) != XGL_ERR_INVALID_PARAM ||\n'
                           '        memcmp(&window, &before, sizeof(window)) != 0) return 1;\n'
                           f'    if (xgl_window_init(&window, {capacity}U) != XGL_OK) return 2;\n'
                           f'    for (unsigned round = 0; round < 3U; ++round) {{\n'
                           f'        for (unsigned i = 0; i < {capacity}U; ++i) {{\n'
                           '            if (!xgl_window_can_send_packet_number(&window)) return 3;\n'
                           '            xgl_window_advance_next_packet_number(&window);\n'
                           '        }\n'
                           '        if (xgl_window_can_send_packet_number(&window)) return 4;\n'
                           f'        for (unsigned i = 0; i < {capacity}U; ++i)\n'
                           '            if (xgl_window_mark_ack_packet_number(&window,\n'
                           '                window.send_base_packet_number + i) != XGL_OK) return 5;\n'
                           f'        if (xgl_window_advance_base_packet_number(&window) != {capacity}U) return 6;\n'
                           '    }\n    return 0;\n}\n', window=True)

    def test_custom_identity_distinguishes_every_layout_option(self):
        combinations = ({}, {"buckets": "2"}, {"capacity": "8"}, {"adaptive": "OFF"},
                        {"chunk": "32"}, {"heap": "OFF"})
        ids = []
        for index, options in enumerate(combinations):
            binary = self.configure(f"identity-{index}", **options)
            config = self.header(binary).read_text(encoding="utf-8")
            line = next(item for item in config.splitlines()
                        if item.startswith("#define XGL_BUILD_CONFIG_ID "))
            ids.append(line)
        self.assertEqual(len(set(ids)), len(ids), "Different SDK resources must have distinct identities")

    def test_generated_header_rejects_conflicting_resource_override(self):
        binary = self.configure("override", buckets="2", capacity="8", adaptive="OFF")
        for definition in ("XGL_RELIABLE_BUCKET_COUNT=32", "XGL_BUILD_WINDOW_CAPACITY=128",
                           "XGL_FEATURE_ADAPTIVE_RTO=1", "XGL_EXPECTED_RESOURCE_CONFIG_ID=0"):
            with self.subTest(definition=definition):
                self.probe("override-" + definition.split("=")[0], binary,
                           '#include <xgl/xgl_build_config.h>\nint main(void) { return 0; }\n',
                           definitions=definition, succeeds=False)

    def test_custom_sdk_exports_matching_identity_and_capacities(self):
        binary = self.configure("sdk", profile="boot", buckets="2", capacity="1", adaptive="OFF")
        self.execute("cmake", "--build", binary, "--parallel", "4")
        prefix = self.root / "install"
        self.execute("cmake", "--install", binary, "--prefix", prefix)
        source = self.root / "sdk-consumer"
        source.mkdir()
        (source / "main.c").write_text(
            '#include <xgl/xgl.h>\n'
            '_Static_assert(XGL_BUILD_CONFIG_ID != 301U, "custom SDK identity");\n'
            '_Static_assert(XGL_RELIABLE_BUCKET_COUNT == 2U, "installed buckets");\n'
            '_Static_assert(XGL_BUILD_WINDOW_CAPACITY == 1U, "installed window");\n'
            '_Static_assert(XGL_FEATURE_ADAPTIVE_RTO == 0, "fixed timeout");\n'
            'int main(void) {\n'
            '    xgl_config_t config;\n    xgl_memory_requirements_t requirements;\n'
            '    xgl_config_get_preset_boot(&config);\n'
            '    if (xgl_memory_requirements(&config, &requirements) != XGL_OK) return 1;\n'
            '    config.protocol.window_size = 2U;\n'
            '    if (xgl_config_validate(&config) != XGL_ERR_INVALID_PARAM) return 2;\n'
            '    return 0;\n}\n', encoding="utf-8")
        (source / "CMakeLists.txt").write_text(
            'cmake_minimum_required(VERSION 3.24)\nproject(resource_sdk_consumer C)\n'
            'find_package(xgl CONFIG REQUIRED COMPONENTS reliable)\n'
            'get_target_property(identity xgl::xgl XGL_RESOURCE_CONFIG_ID)\n'
            'if(NOT identity STREQUAL xgl_RESOURCE_CONFIG_ID)\n'
            '  message(FATAL_ERROR "Target/package resource identities disagree")\nendif()\n'
            'add_executable(probe main.c)\ntarget_link_libraries(probe PRIVATE xgl::xgl)\n'
            'if(CONFLICT_IDENTITY)\n'
            '  add_library(conflict INTERFACE)\n'
            '  set_property(TARGET conflict PROPERTY INTERFACE_XGL_RESOURCE_CONFIG_ID conflict)\n'
            '  target_link_libraries(probe PRIVATE conflict)\nendif()\n', encoding="utf-8")
        command = ["cmake", "-S", source, "-G", "Ninja",
                   f"-DCMAKE_PREFIX_PATH={prefix.as_posix()}"]
        if ARGS.c_compiler:
            command.append(f"-DCMAKE_C_COMPILER={ARGS.c_compiler}")
        build = self.root / "sdk-build"
        self.execute(*command, "-B", build)
        self.execute("cmake", "--build", build)
        executable = build / ("probe.exe" if (build / "probe.exe").is_file() else "probe")
        self.execute(executable)
        rejected = self.execute(*command, "-B", self.root / "sdk-conflict",
                                "-DCONFLICT_IDENTITY=ON", succeeds=False)
        self.assertIn("XGL_RESOURCE_CONFIG_ID", rejected.stdout + rejected.stderr)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--link-source", type=Path, required=True)
    parser.add_argument("--work-dir", type=Path, required=True)
    parser.add_argument("--c-compiler")
    for package in PACKAGES:
        parser.add_argument(f"--{package}-source", type=Path, required=True)
    ARGS, remaining = parser.parse_known_args()
    for name, value in vars(ARGS).items():
        if isinstance(value, Path):
            setattr(ARGS, name, value.resolve())
    unittest.main(argv=[__file__, *remaining])
