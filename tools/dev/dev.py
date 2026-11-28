#!/usr/bin/env python3
"""Prepare and run the maintained XGen Link build and validation entry points."""

import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import signal
import subprocess
import sys
from uuid import uuid4

from environment import DEPENDENCIES, ROOT, build_lock, python_command, run_logged, use_maintenance_tools
from prepare import native_compilers, prepare_actionlint, prepare_arm, prepare_gtest


def git(*args):
    return subprocess.check_output(["git", *args], cwd=ROOT)


def behavior_path(path):
    return ((path.startswith(("cmake/", "include/", "src/", "test/", "examples/"))
             and not path.startswith("test/contracts/tooling/"))
            or path in {"CMakeLists.txt", "CMakePresets.json"})


def tooling_path(path):
    return (path.startswith(("scripts/", "tools/", "dependencies/", "test/contracts/tooling/", ".github/"))
            or path in {".clang-format", ".clang-format-dirs", ".clang-tidy", ".editorconfig",
                        ".gersemirc", ".pre-commit-config.yaml", "requirements.txt"})


def git_paths(*options):
    return [os.fsdecode(path) for path in git(*options, "-z").split(b"\0") if path]


def staged_state():
    staged = git_paths("diff", "--cached", "--no-renames", "--name-only")
    behavior = any(map(behavior_path, staged))
    tooling = any(map(tooling_path, staged))
    if not (behavior or tooling):
        return None
    unstaged = git_paths("diff", "--name-only") + git_paths("ls-files", "--others", "--exclude-standard")
    conflicting = sorted({path for path in unstaged if (behavior and behavior_path(path))
                          or (tooling and tooling_path(path))})
    if conflicting:
        raise ValueError("Test inputs differ from the index:\n" + "\n".join(conflicting))
    return behavior, tooling, git("ls-files", "--stage", "-z")


def preset_directory(preset):
    presets = json.loads((ROOT / "CMakePresets.json").read_text(encoding="utf-8"))
    if preset not in {item["name"] for item in presets["configurePresets"] if not item.get("hidden")}:
        raise ValueError(f"Unknown repository configure preset: {preset}")
    return (ROOT / "build" / preset).resolve()


def build_configuration(directory):
    cache = (directory / "CMakeCache.txt").read_text(encoding="utf-8")
    match = re.search(r"^CMAKE_BUILD_TYPE:STRING=(\S+)$", cache, re.MULTILINE)
    if not match:
        raise ValueError(f"Cannot determine CMake configuration in {directory}")
    return match[1]


def configure_arguments(args):
    command = ["cmake", "--preset", args.preset]
    additional = [arg for arg in getattr(args, "cmake_args", []) if arg != "--"]
    if any(arg in {"--build", "--install", "--preset"} or arg.startswith(("-B", "-S", "--preset="))
           for arg in additional):
        raise ValueError("Use direct CMake for source, binary or preset overrides")
    if args.gtest_dir:
        command.append(f"-DGTest_DIR={args.gtest_dir.resolve()}")
    elif (args.preset.startswith("native-") and not any(os.environ.get(name) for name in ("GTest_DIR", "GTest_ROOT"))
          and not any(arg.startswith("-DGTest_DIR=") for arg in additional)
          and (DEPENDENCIES / "googletest").is_dir()):
        configuration = "Release" if args.preset == "native-release" else "Debug"
        command.append(f"-DGTest_DIR={prepare_gtest(DEPENDENCIES, False, configuration)}")
        if args.preset in {"native-debug", "native-release"}:
            cc, cxx = native_compilers()
            # CMake reads CC/CXX on the initial configure and owns the cached spelling afterwards.
            os.environ.setdefault("CC", cc)
            os.environ.setdefault("CXX", cxx)
    return command + additional


def run_stage(stage, args, directory, logs):
    if stage == "configure":
        command = configure_arguments(args)
    elif stage == "build":
        command = ["cmake", "--build", "--preset", args.preset, "--parallel", str(args.parallel)]
    else:
        configuration = build_configuration(directory)
        command = python_command("test/cmake/run_ctest.py", "--build-dir", directory,
                                 "--config", configuration)
    run_logged(command, logs / f"{stage}.log")


def run_matrix(args):
    staged = args.command == "check" and args.staged
    evidence = staged_state() if staged else None
    if staged and evidence is None:
        print("No staged build, runtime or maintenance-tool inputs")
        return
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    if args.command == "check":
        sequences = () if evidence and not evidence[0] else ("configure", "build", "test")
    else:
        sequences = (args.command,)
    directory = preset_directory(args.preset)
    logs = directory / "developer-commands" / f"{stamp}-{uuid4().hex[:8]}"
    with build_lock(directory):
        logs.mkdir(parents=True)
        identity = {"source_revision": git("rev-parse", "HEAD").decode().strip(),
                    "worktree_status": os.fsdecode(git("status", "--porcelain=v1")),
                    "preset": args.preset}
        if evidence:
            identity["staged_index_sha256"] = hashlib.sha256(evidence[2]).hexdigest()
        (logs / "source.json").write_text(json.dumps(identity, indent=2) + "\n", encoding="utf-8")
        print(f"Command evidence: {logs}")
        if args.command == "check" and not staged:
            run_logged(python_command("scripts/ci/style_gate.py", "--all", "--report",
                                      logs / "style.json"), logs / "style.log")
        for stage in sequences:
            run_stage(stage, args, directory, logs)
        if args.command == "check" and sequences:
            run_logged(python_command("test/contracts/ctest/test_discovery_policy.py"), logs / "discovery.log")
            run_logged(python_command("test/contracts/ctest/test_ctest_evidence.py"), logs / "evidence.log")
        if args.command == "check":
            run_logged([sys.executable, "-m", "unittest", "discover", "-s", "test/contracts/tooling"],
                       logs / "tooling.log")
    if staged and staged_state() != evidence:
        raise ValueError("Staged inputs changed while the gate was running")


def doctor():
    failed = []
    for name in ("git", "cmake", "ninja", "ctest", "clang-format", "gersemi", "pre-commit"):
        executable = shutil.which(name)
        try:
            if not executable:
                raise FileNotFoundError(name)
            output = subprocess.check_output([executable, "--version"], text=True).splitlines()
            print(f"{name}: {executable} ({output[0]})")
        except (OSError, subprocess.SubprocessError, IndexError):
            failed.append(name)
    if failed:
        raise ValueError("Missing or unusable tools: " + ", ".join(failed))


def main(argv=None):
    use_maintenance_tools()
    argv = list(sys.argv[1:] if argv is None else argv)
    if argv[:1] == ["docs"]:
        return subprocess.call(python_command("tools/documentation/cli.py", *argv[1:]), cwd=ROOT)

    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("doctor")
    commands.add_parser("docs", help="forward to the documentation CLI")
    preparation = commands.add_parser("prepare")
    preparation.add_argument("--scope", choices=("host", "workflow", "arm"), required=True)
    preparation.add_argument("--config", choices=("Debug", "Release", "MinSizeRel", "RelWithDebInfo"),
                             default="Debug")
    preparation.add_argument("--directory", type=Path, default=DEPENDENCIES)
    preparation.add_argument("--allow-network", action="store_true")
    for stage in ("configure", "build", "test", "check"):
        command = commands.add_parser(stage)
        command.add_argument("--preset", default="native-debug")
        if stage in ("configure", "check"):
            command.add_argument("--gtest-dir", type=Path)
        if stage in ("build", "check"):
            command.add_argument("--parallel", type=int, choices=range(1, 257), metavar="N", default=4)
        if stage == "check":
            command.add_argument("--staged", action="store_true")
        if stage == "configure":
            command.add_argument("cmake_args", nargs=argparse.REMAINDER)
    args = parser.parse_args(argv)
    try:
        if args.command == "doctor":
            doctor()
        elif args.command == "prepare":
            if args.scope == "host":
                prepared = prepare_gtest(args.directory.resolve(), args.allow_network, args.config)
            elif args.scope == "workflow":
                prepared = prepare_actionlint(args.directory.resolve(), args.allow_network)
            else:
                prepared = prepare_arm(args.directory.resolve(), args.allow_network)
            print(prepared)
        else:
            run_matrix(args)
    except subprocess.CalledProcessError as error:
        print(error, file=sys.stderr)
        return error.returncode if error.returncode > 0 else 128 - error.returncode
    except (ValueError, OSError) as error:
        print(error, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    def interrupt(_signal, _frame):
        raise KeyboardInterrupt()

    if os.name != "nt":
        signal.signal(signal.SIGTERM, interrupt)
    sys.exit(main())
