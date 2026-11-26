"""Verify standalone source and relocated SDK consumption with real CMake builds."""

import argparse
import errno
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


FIXTURES = Path(__file__).resolve().parent
ARGS = None
FORMER_PACKAGES = ("status", "bytes", "crc", "memory", "containers")
PUBLIC_HEADERS = {"xgl.h", "xgl_config.h", "xgl_error.h", "xgl_types.h", "xgl_build_config.h"}
VERIFY_PREFIX = ('include("${CMAKE_CURRENT_LIST_DIR}/VerifySdkPrefix.cmake")\n'
                 'xgl_verify_sdk_prefix("${XGL_SDK_PREFIX}")\n')


def deny_network():
    import ctypes

    # Seccomp filters survive exec and propagate to CMake and its subprocesses.
    seccomp = ctypes.CDLL("libseccomp.so.2")
    seccomp.seccomp_init.argtypes = [ctypes.c_uint32]
    seccomp.seccomp_init.restype = ctypes.c_void_p
    seccomp.seccomp_syscall_resolve_name.argtypes = [ctypes.c_char_p]
    seccomp.seccomp_rule_add.argtypes = [ctypes.c_void_p, ctypes.c_uint32,
                                        ctypes.c_int, ctypes.c_uint]
    seccomp.seccomp_load.argtypes = [ctypes.c_void_p]
    seccomp.seccomp_release.argtypes = [ctypes.c_void_p]
    seccomp.seccomp_release.restype = None

    allow = 0x7FFF0000  # SCMP_ACT_ALLOW
    deny_operation = 0x00050000 | errno.EPERM  # SCMP_ACT_ERRNO(EPERM)
    context = seccomp.seccomp_init(allow)
    if not context:
        raise OSError("libseccomp could not allocate a filter")
    try:
        # io_uring can create sockets without calling the socket syscall.
        for name in (b"socket", b"io_uring_setup"):
            number = seccomp.seccomp_syscall_resolve_name(name)
            if number == -1:
                raise OSError(f"libseccomp could not resolve {name.decode()}")
            status = seccomp.seccomp_rule_add(context, deny_operation, number, 0)
            if status < 0:
                raise OSError(f"could not deny {name.decode()}: {os.strerror(-status)}")
        status = seccomp.seccomp_load(context)
        if status < 0:
            raise OSError(f"could not load seccomp filter: {os.strerror(-status)}")
    finally:
        seccomp.seccomp_release(context)


class StandaloneContracts(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        ARGS.work_dir.mkdir(parents=True, exist_ok=True)
        cls.work = Path(tempfile.mkdtemp(prefix="standalone-", dir=ARGS.work_dir))
        cls.commands = []
        cls.env = {key: value for key, value in os.environ.items()
                   if key not in {"CMAKE_PREFIX_PATH", "CMAKE_MODULE_PATH", "CMAKE_TOOLCHAIN_FILE",
                                  "CPATH", "C_INCLUDE_PATH", "CPLUS_INCLUDE_PATH", "LIBRARY_PATH"}
                   and not key.startswith("XGL_DEV_")}
        cls.isolation = [
            "-DCMAKE_FIND_USE_PACKAGE_REGISTRY=OFF",
            "-DCMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY=OFF",
            "-DFETCHCONTENT_FULLY_DISCONNECTED=ON",
            "-DCMAKE_DISABLE_FIND_PACKAGE_GTest=ON",
            "-DCMAKE_DISABLE_FIND_PACKAGE_Python3=ON",
            "-DCMAKE_DISABLE_FIND_PACKAGE_Doxygen=ON",
        ]
        cls.isolation.extend(f"-DCMAKE_DISABLE_FIND_PACKAGE_xgen_{name}=ON"
                             for name in FORMER_PACKAGES)

    @classmethod
    def tearDownClass(cls):
        (cls.work / "commands.json").write_text(
            json.dumps(cls.commands, indent=2) + "\n", encoding="utf-8")
        print(f"Standalone build evidence: {cls.work}")

    def run_command(self, *command, succeeds=True):
        command = [str(part) for part in command]
        result = subprocess.run(command, env=self.env, capture_output=True,
                                text=True, encoding="utf-8", errors="replace",
                                check=False, timeout=180)
        log = self.work / f"command-{len(self.commands):03d}.log"
        output = result.stdout + result.stderr
        log.write_text(output, encoding="utf-8")
        self.commands.append({"command": command, "exit_code": result.returncode, "log": str(log)})
        self.assertEqual(result.returncode == 0, succeeds, f"{log}\n{output}")
        return output

    def configure(self, source, binary, *options, succeeds=True):
        return self.run_command("cmake", "-S", source, "-B", binary, "-G", "Ninja",
                                *self.isolation, *options, succeeds=succeeds)

    def build(self, binary, succeeds=True):
        return self.run_command("cmake", "--build", binary, "--parallel", "4", succeeds=succeeds)

    def executable(self, binary, name):
        executable = binary / name
        self.run_command(executable if executable.is_file() else executable.with_suffix(".exe"))

    def new_consumer(self, name, language, selection, *, include_consumer=True):
        source = self.work / name
        source.mkdir()
        suffix = "c" if language == "C" else "cpp"
        fixture = f"consumer.{suffix}"
        shutil.copyfile(FIXTURES / "consumer.c", source / fixture)
        shutil.copyfile(FIXTURES / "VerifySdkPrefix.cmake",
                        source / "VerifySdkPrefix.cmake")
        (source / "CMakeLists.txt").write_text(
            "cmake_minimum_required(VERSION 3.24)\n"
            f"project(xgl_consumer {language})\n"
            "set(CMAKE_C_STANDARD 11)\nset(CMAKE_C_STANDARD_REQUIRED ON)\n"
            "set(CMAKE_C_EXTENSIONS OFF)\n"
            "set(CMAKE_CXX_STANDARD 17)\nset(CMAKE_CXX_STANDARD_REQUIRED ON)\n"
            "set(CMAKE_CXX_EXTENSIONS OFF)\n"
            f"{selection}\n" +
            ('get_target_property(sdk_deps xgl::xgl INTERFACE_LINK_LIBRARIES)\n'
             'if(sdk_deps)\n  message(FATAL_ERROR "Unexpected SDK link dependency: ${sdk_deps}")\nendif()\n'
             f'add_executable(consumer {fixture})\n'
             'set_target_properties(consumer PROPERTIES C_EXTENSIONS NO CXX_EXTENSIONS NO)\n'
             f'target_compile_features(consumer PRIVATE {"c_std_11" if language == "C" else "cxx_std_17"})\n'
             'target_link_libraries(consumer PRIVATE xgl::xgl)\n'
             'if(FORCE_STALE_HEADER_ABI)\n'
             '  target_compile_definitions(consumer PRIVATE XGL_CONFIG_ABI_VERSION=3)\nendif()\n'
             'if(FORCE_STALE_TARGET_ABI)\n'
             '  add_library(legacy_abi INTERFACE)\n'
             '  set_property(TARGET legacy_abi PROPERTY INTERFACE_XGL_CONFIG_ABI_VERSION 3)\n'
             '  target_link_libraries(consumer PRIVATE legacy_abi)\nendif()\n'
             if include_consumer else ""),
            encoding="utf-8")
        return source

    def test_subdirectory_defaults_do_not_add_developer_targets(self):
        source = self.new_consumer(
            "parent", "C",
            'add_library(xgs::status INTERFACE IMPORTED)\n'
            'add_library(xgm::allocator INTERFACE IMPORTED)\n'
            f'add_subdirectory("{ARGS.link_source.as_posix()}" production)\n'
            'foreach(target IN ITEMS xgl_tests xgl_docs xgl_noheap xgl_footprint\n'
            '        xgl_release_validation xgl_sdk_consumer_smoke xgl_static_analysis)\n'
            '  if(TARGET ${target})\n'
            '    message(FATAL_ERROR "Unexpected developer target: ${target}")\n'
            '  endif()\n'
            'endforeach()')
        binary = self.work / "parent-build"
        self.configure(source, binary, "-DCMAKE_CXX_COMPILER=unavailable")
        self.build(binary)
        self.executable(binary, "consumer")

    def test_offline_source_and_relocated_sdk_reject_foreign_abi(self):
        if ARGS.deny_network:
            self.run_command(sys.executable, "-c", """
import errno
import socket

for family in (socket.AF_INET, socket.AF_INET6):
    try:
        with socket.socket(family):
            raise SystemExit(f"{family.name}: socket creation was allowed")
    except PermissionError as error:
        if error.errno != errno.EPERM:
            raise
        print(f"{family.name}: socket creation denied with EPERM")
""")

        source = self.work / "production-source"
        source.mkdir()
        shutil.copyfile(ARGS.link_source / "CMakeLists.txt", source / "CMakeLists.txt")
        for directory in ("cmake", "include", "src"):
            shutil.copytree(ARGS.link_source / directory, source / directory)
        prefixes = {}
        for profile in ("boot", "full"):
            with self.subTest(profile=profile):
                binary = self.work / (profile + "-production-build")
                self.configure(source, binary, f"-DXGL_PROFILE={profile}",
                               "-DCMAKE_CXX_COMPILER=unavailable", "-DCMAKE_BUILD_TYPE=Release")
                self.assertFalse(list(binary.glob("CMakeFiles/*/CMakeCXXCompiler.cmake")))
                self.build(binary)
                stage = self.work / (profile + "-stage")
                self.run_command("cmake", "--install", binary, "--prefix", stage)
                prefix = stage.rename(self.work / (profile + "-relocated"))
                prefixes[profile] = prefix
                self.assertEqual({item.name for item in (prefix / "include/xgl").iterdir()},
                                 PUBLIC_HEADERS)
                self.assertEqual({item.name for item in (prefix / "include").iterdir()}, {"xgl"})
                shutil.rmtree(binary)
        shutil.rmtree(source)

        boot = f'-DBOOT_PREFIX={prefixes["boot"].as_posix()}'
        full = f'-DFULL_PREFIX={prefixes["full"].as_posix()}'
        cases = (
            (
                "required-then-full",
                'find_package(xgl 4 QUIET CONFIG COMPONENTS authentication\n'
                '    PATHS "${BOOT_PREFIX}" NO_DEFAULT_PATH)\n'
                'if(xgl_FOUND OR TARGET xgl::xgl OR xgl_authentication_FOUND)\n'
                '  message(FATAL_ERROR "Failed required capability imported a target")\nendif()\n'
                'if(NOT xgl_NOT_FOUND_MESSAGE MATCHES "Unsupported xgl capability: authentication")\n'
                '  message(FATAL_ERROR "Missing required capability diagnostic")\nendif()\n'
                'unset(xgl_DIR CACHE)\n'
                'find_package(xgl 4 CONFIG REQUIRED COMPONENTS authentication\n'
                '    PATHS "${FULL_PREFIX}" NO_DEFAULT_PATH)\n'
                'if(NOT xgl_authentication_FOUND OR DEFINED xgl_NOT_FOUND_MESSAGE)\n'
                '  message(FATAL_ERROR "Successful retry retained failed capability state")\nendif()\n'
                'set(XGL_SDK_PREFIX "${FULL_PREFIX}")\n' + VERIFY_PREFIX,
                None,
            ),
            (
                "optional-missing",
                'find_package(xgl 4 CONFIG REQUIRED COMPONENTS reliable\n'
                '    OPTIONAL_COMPONENTS authentication PATHS "${BOOT_PREFIX}" NO_DEFAULT_PATH)\n'
                'if(xgl_authentication_FOUND OR NOT xgl_reliable_FOUND OR\n'
                '    NOT TARGET xgl::xgl OR DEFINED xgl_NOT_FOUND_MESSAGE)\n'
                '  message(FATAL_ERROR "Optional missing capability invalidated the package")\nendif()\n'
                'set(XGL_SDK_PREFIX "${BOOT_PREFIX}")\n' + VERIFY_PREFIX,
                None,
            ),
            (
                "later-incompatible",
                'find_package(xgl 4 CONFIG REQUIRED PATHS "${BOOT_PREFIX}" NO_DEFAULT_PATH)\n'
                'unset(xgl_DIR CACHE)\n'
                'find_package(xgl 4 QUIET CONFIG OPTIONAL_COMPONENTS authentication\n'
                '    PATHS "${FULL_PREFIX}" NO_DEFAULT_PATH)\n',
                "xgl::xgl conflicts with the requested SDK resource identity",
            ),
        )
        for name, selection, diagnostic in cases:
            with self.subTest(component_selection=name):
                consumer = self.new_consumer(
                    name, "C", selection, include_consumer=diagnostic is None)
                binary = self.work / (name + "-build")
                output = self.configure(consumer, binary, boot, full,
                                        succeeds=diagnostic is None)
                if diagnostic is None:
                    self.build(binary)
                    self.executable(binary, "consumer")
                else:
                    self.assertIn(diagnostic, output)

        empty_prefix = self.work / "empty-prefix"
        empty_prefix.mkdir()
        location = f"-DXGL_SDK_PREFIX={empty_prefix}"
        ambient = self.new_consumer(
            "ambient-sdk", "C",
            'set(ENV{xgl_ROOT} "${BOOT_PREFIX}")\n'
            'find_package(xgl 4 QUIET CONFIG PATHS "${XGL_SDK_PREFIX}" NO_DEFAULT_PATH)\n'
            'if(xgl_FOUND OR TARGET xgl::xgl)\n'
            '  message(FATAL_ERROR "Found ambient xgl_ROOT despite empty expected prefix")\nendif()\n',
            include_consumer=False)
        self.configure(ambient, self.work / "ambient-sdk-build", boot, location)

        boot_config_dir = next(prefixes["boot"].rglob("xglConfig.cmake")).parent
        wrong = self.new_consumer(
            "wrong-xgl-dir", "C",
            'set(xgl_DIR "${BOOT_CONFIG_DIR}")\n'
            'find_package(xgl 4 CONFIG REQUIRED PATHS "${XGL_SDK_PREFIX}" NO_DEFAULT_PATH)\n'
            + VERIFY_PREFIX, include_consumer=False)
        output = self.configure(wrong, self.work / "wrong-xgl-dir-build",
                                f"-DBOOT_CONFIG_DIR={boot_config_dir}", location,
                                succeeds=False)
        self.assertIn("Selected SDK config is outside the expected prefix", output)

        for profile, prefix in prefixes.items():
            for language in ("C", "CXX"):
                with self.subTest(profile=profile, language=language):
                    suffix = f"{profile}-{language}"
                    consumer = self.new_consumer(
                        "sdk-" + suffix, language,
                        'find_package(xgl 4 CONFIG REQUIRED COMPONENTS reliable\n'
                        '    PATHS "${XGL_SDK_PREFIX}" NO_DEFAULT_PATH)\n'
                        + VERIFY_PREFIX +
                        "if(NOT xgl_CONFIG_ABI_VERSION EQUAL 4)\n"
                        '  message(FATAL_ERROR "Unexpected SDK ABI")\n'
                        "endif()")
                    binary = self.work / ("sdk-build-" + suffix)
                    location = f"-DXGL_SDK_PREFIX={prefix.as_posix()}"
                    self.configure(consumer, binary, location)
                    self.build(binary)
                    self.executable(binary, "consumer")
                    if language == "C":
                        conflict = self.work / ("sdk-conflict-" + profile)
                        output = self.configure(consumer, conflict, location,
                                                "-DFORCE_STALE_TARGET_ABI=ON", succeeds=False)
                        self.assertIn("XGL_CONFIG_ABI_VERSION", output)
                        stale = self.work / ("sdk-stale-" + profile)
                        self.configure(consumer, stale, location, "-DFORCE_STALE_HEADER_ABI=ON")
                        output = self.build(stale, succeeds=False)
                        self.assertIn("XGL_CONFIG_ABI_VERSION conflicts", output)

        for name, selection, diagnostic in (
            ("legacy-sdk", "find_package(xgl 3 CONFIG REQUIRED", 'requested version "3"'),
            ("boot-auth", "find_package(xgl 4 CONFIG REQUIRED COMPONENTS authentication",
             "Unsupported xgl capability: authentication"),
        ):
            consumer = self.new_consumer(
                name, "C", selection + ' PATHS "${XGL_SDK_PREFIX}" NO_DEFAULT_PATH)\n',
                include_consumer=False)
            output = self.configure(consumer, self.work / (name + "-build"),
                                    f"-DXGL_SDK_PREFIX={prefixes['boot'].as_posix()}",
                                    succeeds=False)
            self.assertIn(diagnostic, output)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--link-source", type=Path, required=True)
    parser.add_argument("--work-dir", type=Path, required=True)
    parser.add_argument("--deny-network", action="store_true",
                        help="deny network socket creation using Linux libseccomp")
    ARGS, remaining = parser.parse_known_args()
    ARGS.link_source = ARGS.link_source.resolve()
    ARGS.work_dir = ARGS.work_dir.resolve()
    if ARGS.deny_network:
        if sys.platform != "linux":
            parser.error("--deny-network requires Linux and libseccomp.so.2")
        try:
            deny_network()
        except OSError as error:
            parser.error(f"--deny-network: {error}")
    unittest.main(argv=[__file__, *remaining])
