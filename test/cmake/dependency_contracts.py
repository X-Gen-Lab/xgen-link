"""Exercise dependency ownership through real configure/build consumers."""

import argparse
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import unittest


PACKAGES = ("status", "bytes", "crc", "memory", "containers")
REQUIRED = {
    "xgs::status": ("XGS", "status"),
    "xgb::bytes": ("XGB", "bytes"),
    "xgcrc::crc16": ("XGCRC", "crc"),
    "xgm::allocator": ("XGM", "memory"),
    "xgm::pool": ("XGM", "memory"),
    "xgm::size_class": ("XGM", "memory"),
    "xgct::list": ("XGCT", "containers"),
    "xgct::bitset": ("XGCT", "containers"),
    "xgct::hash": ("XGCT", "containers"),
}
OPTIONS = (
    "XGL_BUILD_TESTS", "XGL_BUILD_EXAMPLES", "XGL_BUILD_DOCS",
    "XGL_BUILD_NOHEAP_SMOKE", "XGL_BUILD_SDK_CONSUMER_SMOKE",
    "XGL_BUILD_RELEASE_VALIDATION_TARGET", "XGL_BUILD_STATIC_ANALYSIS_TARGET",
    "XGL_BUILD_FOOTPRINT_REPORT",
)
ROOT = Path(__file__).resolve().parent
ARGS = None


def cmake_path(path):
    value = Path(path).resolve().as_posix()
    if '"' in value or ";" in value or "\n" in value:
        raise ValueError(f"Unsupported CMake test path: {path}")
    return f'"{value}"'


def parent_sources():
    lines = [
        'set(XGS_BUILD_STRINGS ON)',
        'set(XGCRC_BUILD_CRC8 ON)', 'set(XGCRC_BUILD_CRC16 ON)',
        'set(XGM_COMPONENTS "allocator;pool;size_class;arena;tracking;libc_allocator")',
        'set(XGCT_BUILD_LIST ON)', 'set(XGCT_BUILD_HASH ON)',
        'set(XGCT_BUILD_BITSET ON)', 'set(XGCT_BUILD_RING_BUFFER ON)',
    ]
    for package in PACKAGES:
        lines.append(f'add_subdirectory({cmake_path(getattr(ARGS, package + "_source"))} '
                     f'"${{CMAKE_CURRENT_BINARY_DIR}}/modules/{package}")')
    return "\n".join(lines) + "\n"


def disable_helpers():
    return "".join(f"set({name} OFF)\n" for name in OPTIONS)


def preamble():
    return ('cmake_minimum_required(VERSION 3.24)\n'
            'project(dependency_contract LANGUAGES C)\n'
            'set(CMAKE_C_STANDARD 11)\n'
            'set(CMAKE_C_STANDARD_REQUIRED ON)\n'
            'set(CMAKE_C_EXTENSIONS OFF)\n'
            'set(CMAKE_FIND_PACKAGE_TARGETS_GLOBAL TRUE)\n')


def load_link(path=None, profile="full"):
    return (f'set(XGL_PROFILE {profile})\n'
            'set(XGL_ALLOW_FALLBACK_MALLOC OFF)\n'
            f'add_subdirectory({cmake_path(path or ARGS.link_source)} '
            '"${CMAKE_CURRENT_BINARY_DIR}/protocol")\n')


def consumer(application=False):
    source = ROOT / ("application.c" if application else "consumer.c")
    libraries = "xgl::xgl xgct::ring_buffer xgm::arena" if application else "xgl::xgl"
    return (f'add_executable(consumer {cmake_path(source)})\n'
            f'target_link_libraries(consumer PRIVATE {libraries})\n')


def origins():
    lines = ['set(origins "")']
    for target in REQUIRED:
        artifact = "-" if target == "xgs::status" else f"$<TARGET_FILE:{target}>"
        lines += [
            f'get_target_property(origin_source {target} SOURCE_DIR)',
            f'get_target_property(origin_imported {target} IMPORTED)',
            f'string(APPEND origins "{target}|${{origin_source}}|${{origin_imported}}|{artifact}\\n")',
        ]
    lines.append('file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/origins-$<CONFIG>.txt" CONTENT "${origins}")')
    return "\n".join(lines) + "\n"


class DependencyContracts(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        ARGS.work_dir.mkdir(parents=True, exist_ok=True)
        cls.run_root = Path(tempfile.mkdtemp(prefix="run-", dir=ARGS.work_dir))
        cls.counter = 0
        cls.commands = []
        cls.prefix = cls.run_root / "installed-dependencies"
        cls.build_project("prepare", parent_sources(), build=True, install=True)

    @classmethod
    def execute(cls, command, cwd, expected=0):
        cls.counter += 1
        result = subprocess.run([str(part) for part in command], cwd=cwd,
                                capture_output=True, text=True, encoding="utf-8",
                                errors="replace", timeout=240, check=False)
        log = cls.run_root / f"command-{cls.counter:03d}.log"
        log.write_text(result.stdout + result.stderr, encoding="utf-8")
        cls.commands.append({"command": [str(part) for part in command],
                             "cwd": str(cwd), "exit_code": result.returncode,
                             "log": str(log)})
        if expected == 0 and result.returncode != 0:
            raise AssertionError(f"Command failed ({result.returncode}): {command}\n"
                                 f"{result.stdout}{result.stderr}\nLog: {log}")
        return result

    @classmethod
    def build_project(cls, name, body, *, build=False, install=False,
                      package_prefix=False, definitions=(), expect_failure=False):
        source = cls.run_root / name / "source"
        binary = cls.run_root / name / "build"
        source.mkdir(parents=True)
        (source / "CMakeLists.txt").write_text(preamble() + body, encoding="utf-8")
        command = [ARGS.cmake, "-S", source, "-B", binary, "-G", ARGS.generator,
                   f"-DCMAKE_C_COMPILER={ARGS.c_compiler}",
                   f"-DCMAKE_BUILD_TYPE={ARGS.config}",
                   "-DCMAKE_FIND_USE_PACKAGE_REGISTRY=OFF",
                   "-DCMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY=OFF",
                   "-DCMAKE_FIND_USE_CMAKE_ENVIRONMENT_PATH=OFF",
                   f"-DCMAKE_INSTALL_PREFIX={cls.prefix}"]
        if package_prefix:
            command.append(f"-DCMAKE_PREFIX_PATH={cls.prefix.as_posix()}")
        command.extend(definitions)
        result = cls.execute(command, source, expected=None if expect_failure else 0)
        if expect_failure:
            return result, binary
        if build:
            cls.execute([ARGS.cmake, "--build", binary, "--config", ARGS.config,
                         "--parallel", "2"], source)
        if install:
            cls.execute([ARGS.cmake, "--install", binary, "--config", ARGS.config], source)
        return result, binary

    def run_consumer(self, binary):
        candidates = [binary / "consumer", binary / "consumer.exe",
                      binary / ARGS.config / "consumer.exe",
                      binary / ARGS.config / "consumer"]
        executable = next((path for path in candidates if path.is_file()), None)
        self.assertIsNotNone(executable, f"Missing consumer executable: {binary}")
        self.execute([executable], binary)

    def assert_rejected(self, result, pattern):
        self.assertNotEqual(result.returncode, 0, "Configuration unexpectedly accepted the input")
        self.assertRegex(result.stdout + result.stderr, pattern)

    def test_parent_capability_union_is_reused_and_application_builds(self):
        body = parent_sources() + disable_helpers() + load_link() + origins() + consumer(True)
        _, binary = self.build_project("parent-union", body, build=True)
        self.run_consumer(binary)
        for line in (binary / f"origins-{ARGS.config}.txt").read_text("utf-8").splitlines():
            target, source, imported, artifact = line.split("|")
            expected = getattr(ARGS, REQUIRED[target][1] + "_source")
            self.assertEqual(Path(source).resolve(), expected.resolve(), target)
            self.assertIn(imported, ("FALSE", "0"), target)
            if artifact != "-":
                self.assertTrue(Path(artifact).is_file(), target)
                self.assertTrue(Path(artifact).resolve().is_relative_to(binary / "modules"), target)

    def test_installed_packages_are_used_without_loading_source_checkouts(self):
        body = disable_helpers() + load_link() + origins() + consumer()
        _, binary = self.build_project("installed", body, build=True, package_prefix=True)
        self.run_consumer(binary)
        for line in (binary / f"origins-{ARGS.config}.txt").read_text("utf-8").splitlines():
            target, _, imported, artifact = line.split("|")
            self.assertIn(imported, ("TRUE", "1"), f"{target} came from source instead of the package")
            if artifact != "-":
                self.assertTrue(Path(artifact).is_file(), target)
                self.assertTrue(Path(artifact).resolve().is_relative_to(self.prefix), target)

    def test_subdirectory_defaults_do_not_add_developer_targets(self):
        body = parent_sources() + load_link()
        for name in OPTIONS:
            body += f'if({name})\n  message(FATAL_ERROR "Subdirectory default enabled: {name}")\nendif()\n'
        body += ('foreach(name xgl_noheap xgl_static_workspace_smoke xgl_noheap_smoke '
                 'xgl_sdk_consumer_smoke xgl_release_validation xgl_static_analysis '
                 'xgl_footprint echo_server file_transfer multi_node boot_update)\n'
                 '  if(TARGET ${name})\n'
                 '    message(FATAL_ERROR "Unexpected helper target: ${name}")\n'
                 '  endif()\nendforeach()\n')
        self.build_project("subdirectory-defaults", body)

    def test_legacy_source_options_give_migration_errors(self):
        for package in PACKAGES:
            with self.subTest(package=package):
                option = f"XGL_{package.upper()}_SOURCE_DIR"
                body = (parent_sources() + disable_helpers() +
                        f"set({option} {cmake_path(getattr(ARGS, package + '_source'))})\n" + load_link())
                result, _ = self.build_project(f"legacy-{package}", body, expect_failure=True)
                self.assert_rejected(result, option)
                self.assertRegex(result.stdout + result.stderr,
                                 r"(?i)removed|no longer|parent|find_package|migration")

    def test_external_checkout_is_never_evaluated(self):
        copy = self.run_root / "external-sentinel-link"
        copy.mkdir()
        shutil.copy2(ARGS.link_source / "CMakeLists.txt", copy / "CMakeLists.txt")
        for name in ("cmake", "src", "include"):
            shutil.copytree(ARGS.link_source / name, copy / name)
        trap = copy / "external/xgen-status"
        trap.mkdir(parents=True)
        marker = self.run_root / "external-was-evaluated.txt"
        (trap / "CMakeLists.txt").write_text(
            f'file(WRITE {cmake_path(marker)} "unexpected source loading")\n'
            'message(FATAL_ERROR "EXTERNAL_CHECKOUT_EVALUATED")\n', encoding="utf-8")
        body = disable_helpers() + load_link(copy)
        result, _ = self.build_project("external-sentinel", body, expect_failure=True,
                                       definitions=("-DCMAKE_DISABLE_FIND_PACKAGE_xgen_status=ON",))
        self.assertFalse(marker.exists(), "link evaluated the nested dependency CMakeLists")
        self.assert_rejected(result, "xgen_status")

    def test_missing_dependency_is_rejected_without_implicit_source_fallback(self):
        result, _ = self.build_project("missing-package", disable_helpers() + load_link(),
                                       expect_failure=True,
                                       definitions=("-DCMAKE_DISABLE_FIND_PACKAGE_xgen_status=ON",))
        self.assert_rejected(result, "xgen_status|xgs::status")

    def test_partial_parent_targets_require_the_missing_component(self):
        body = disable_helpers()
        for target, (prefix, _) in REQUIRED.items():
            if target == "xgct::bitset":
                continue
            kind = "INTERFACE" if target == "xgs::status" else "STATIC"
            body += (f"add_library({target} {kind} IMPORTED GLOBAL)\n"
                     f"set_target_properties({target} PROPERTIES {prefix}_VERSION 0.1.0 "
                     f"{prefix}_ABI_VERSION 1)\n")
        body += load_link()
        result, _ = self.build_project("partial-targets", body, expect_failure=True,
                                       definitions=("-DCMAKE_DISABLE_FIND_PACKAGE_xgen_containers=ON",))
        self.assert_rejected(result, "xgct::bitset|xgen_containers")

    def test_incompatible_parent_identity_is_rejected(self):
        for property_name, value in (("XGM_ABI_VERSION", "2"), ("XGM_VERSION", "0.2.0"),
                                     ("XGM_VERSION", "0.1.1")):
            with self.subTest(property=property_name, value=value):
                body = (parent_sources() + disable_helpers() +
                        f"set_target_properties(xgm_pool PROPERTIES {property_name} {value})\n" + load_link())
                result, _ = self.build_project(f"identity-{property_name}-{value}", body,
                                               expect_failure=True)
                self.assert_rejected(result, "xgm::pool|one package must use one version")

    def test_wrong_target_kind_is_rejected(self):
        body = disable_helpers()
        for target, (prefix, _) in REQUIRED.items():
            kind = "INTERFACE" if target in ("xgs::status", "xgm::pool") else "STATIC"
            body += (f"add_library({target} {kind} IMPORTED GLOBAL)\n"
                     f"set_target_properties({target} PROPERTIES {prefix}_VERSION 0.1.0 "
                     f"{prefix}_ABI_VERSION 1)\n")
        result, _ = self.build_project("wrong-kind", body + load_link(), expect_failure=True)
        self.assert_rejected(result, "xgm::pool.*requires|STATIC_LIBRARY")

    def test_full_and_boot_have_independent_configuration_and_builds(self):
        ids = {}
        for profile, expected in (("full", 303), ("boot", 301)):
            with self.subTest(profile=profile):
                body = (parent_sources() + disable_helpers() + load_link(profile=profile) +
                        'get_target_property(libraries xgl LINK_LIBRARIES)\n'
                        'file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/link-libraries.txt" "${libraries}")\n' +
                        consumer())
                _, binary = self.build_project(f"profile-{profile}", body, build=True)
                self.run_consumer(binary)
                generated = (binary / "protocol/generated/xgl/xgl_build_config.h").read_text("utf-8")
                match = re.search(r"#define\s+XGL_BUILD_CONFIG_ID\s+(\d+)", generated)
                self.assertIsNotNone(match)
                ids[profile] = int(match[1])
                self.assertEqual(ids[profile], expected)
                libraries = (binary / "link-libraries.txt").read_text("utf-8")
                self.assertEqual("xgct::hash" in libraries, profile == "full")
        self.assertNotEqual(ids["full"], ids["boot"])


def main():
    global ARGS
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--link-source", type=Path, required=True)
    parser.add_argument("--work-dir", type=Path, required=True)
    for package in PACKAGES:
        parser.add_argument(f"--{package}-source", type=Path, required=True)
    parser.add_argument("--cmake", default="cmake")
    parser.add_argument("--generator", default="Ninja")
    parser.add_argument("--c-compiler", default="gcc")
    parser.add_argument("--config", default="Debug")
    parser.add_argument("--case", action="append", help="Specific unittest method; repeat to select several")
    ARGS = parser.parse_args()
    ARGS.link_source = ARGS.link_source.resolve()
    ARGS.work_dir = ARGS.work_dir.resolve()
    for package in PACKAGES:
        source = getattr(ARGS, package + "_source").resolve()
        if not (source / "CMakeLists.txt").is_file():
            parser.error(f"Missing explicitly supplied {package} checkout: {source}")
        setattr(ARGS, package + "_source", source)
    if not (ARGS.link_source / "CMakeLists.txt").is_file():
        parser.error("--link-source must contain CMakeLists.txt")
    suite = (unittest.TestSuite(DependencyContracts(name) for name in ARGS.case) if ARGS.case
             else unittest.defaultTestLoader.loadTestsFromTestCase(DependencyContracts))
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    if hasattr(DependencyContracts, "run_root"):
        report = DependencyContracts.run_root / "result.json"
        report.write_text(json.dumps({
            "tests_run": result.testsRun,
            "failures": [{"test": str(test), "traceback": trace} for test, trace in result.failures],
            "errors": [{"test": str(test), "traceback": trace} for test, trace in result.errors],
            "commands": DependencyContracts.commands,
        }, indent=2) + "\n", encoding="utf-8")
        print(f"Contract evidence: {report}")
    return 0 if result.wasSuccessful() else 1


if __name__ == "__main__":
    sys.exit(main())
