"""Exercise bounded RX chunk selection and installed headers."""

import argparse
import json
from pathlib import Path
import subprocess
import tempfile
import unittest


ARGS = None


class RxChunkConfig(unittest.TestCase):
    """Use real CMake configurations and compile the public configuration header."""

    @classmethod
    def setUpClass(cls):
        ARGS.work_dir.mkdir(parents=True, exist_ok=True)
        cls.root = Path(tempfile.mkdtemp(prefix="rx-chunk-", dir=ARGS.work_dir))
        cls.commands = []

    @classmethod
    def tearDownClass(cls):
        (cls.root / "commands.json").write_text(
            json.dumps(cls.commands, indent=2) + "\n", encoding="utf-8")
        print(f"RX chunk evidence: {cls.root}")

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

    def configure(self, name, profile, chunk=None, succeeds=True):
        binary = self.root / name
        command = ["cmake", "-S", ARGS.link_source, "-B", binary, "-G", "Ninja",
                   f"-DXGL_PROFILE={profile}", "-DCMAKE_BUILD_TYPE=Debug",
                   "-DXGL_BUILD_TESTS=OFF", "-DXGL_BUILD_EXAMPLES=OFF",
                   "-DXGL_BUILD_DOCS=OFF", "-DXGL_BUILD_FOOTPRINT_REPORT=OFF",
                   "-DXGL_BUILD_NOHEAP_SMOKE=OFF", "-DXGL_BUILD_SDK_CONSUMER_SMOKE=OFF",
                   "-DXGL_BUILD_RELEASE_VALIDATION_TARGET=OFF",
                   "-DXGL_ALLOW_FALLBACK_MALLOC=OFF"]
        if chunk is not None:
            command.append(f"-DXGL_DATALINK_RX_CHUNK_SIZE={chunk}")
        if ARGS.c_compiler:
            command.append(f"-DCMAKE_C_COMPILER={ARGS.c_compiler}")
        self.execute(*command, succeeds=succeeds)
        return binary

    def check_header(self, name, includes, expected, override=None, succeeds=True):
        source = self.root / (name + "-consumer")
        binary = source / "build"
        source.mkdir(parents=True, exist_ok=True)
        (source / "main.c").write_text(
            '#include <xgl/xgl_config.h>\n'
            f'_Static_assert(XGL_DATALINK_RX_CHUNK_SIZE == {expected}U, "RX chunk contract");\n'
            'int main(void) { return 0; }\n', encoding="utf-8")
        definitions = (f'target_compile_definitions(probe PRIVATE XGL_DATALINK_RX_CHUNK_SIZE={override})\n'
                       if override is not None else "")
        (source / "CMakeLists.txt").write_text(
            'cmake_minimum_required(VERSION 3.24)\nproject(rx_chunk_consumer C)\n'
            'add_executable(probe main.c)\ntarget_compile_features(probe PRIVATE c_std_11)\n'
            'target_include_directories(probe PRIVATE\n' +
            ''.join(f'    "{path.as_posix()}"\n' for path in includes) + ')\n' + definitions,
            encoding="utf-8")
        command = ["cmake", "-S", source, "-B", binary, "-G", "Ninja"]
        if ARGS.c_compiler:
            command.append(f"-DCMAKE_C_COMPILER={ARGS.c_compiler}")
        self.execute(*command)
        return self.execute("cmake", "--build", binary, succeeds=succeeds)

    def source_includes(self, binary):
        return [ARGS.link_source / "include", binary / "generated"]

    def test_auto_follows_profile_without_stale_cache(self):
        for profile, expected in (("boot", 32), ("full", 128), ("boot", 32)):
            binary = self.configure("switch", profile)
            self.check_header("switch-" + profile, self.source_includes(binary), expected)

    def test_nonboot_defaults_remain_128(self):
        for profile in ("full", "embedded", "embedded-lite"):
            with self.subTest(profile=profile):
                binary = self.configure(profile, profile)
                self.check_header(profile, self.source_includes(binary), 128)

    def test_explicit_capacity_survives_installation(self):
        binary = self.configure("custom", "boot", "64")
        self.check_header("custom-source", self.source_includes(binary), 64)
        self.execute("cmake", "--build", binary, "--parallel", "4")
        prefix = self.root / "install"
        self.execute("cmake", "--install", binary, "--prefix", prefix)
        self.check_header("custom-installed", [prefix / "include"], 64)

    def test_configuration_rejects_unsupported_capacity(self):
        for value in ("0", "-1", "17", "256", "invalid"):
            with self.subTest(value=value):
                self.configure("invalid-" + value, "boot", value, succeeds=False)

    def test_consumer_cannot_override_compiled_capacity(self):
        binary = self.configure("override", "boot", "64")
        self.check_header("override-match", self.source_includes(binary), 64, override=64)
        self.check_header("override-conflict", self.source_includes(binary), 32,
                          override=32, succeeds=False)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--link-source", type=Path, required=True)
    parser.add_argument("--work-dir", type=Path, required=True)
    parser.add_argument("--c-compiler")
    ARGS, remaining = parser.parse_known_args()
    for name, value in vars(ARGS).items():
        if isinstance(value, Path):
            setattr(ARGS, name, value.resolve())
    unittest.main(argv=[__file__, *remaining])
