#!/usr/bin/env python3
"""Analyze each actual production translation unit in one configured profile."""

import argparse
from concurrent.futures import ThreadPoolExecutor
import json
from pathlib import Path
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[2]
TIDY_CHECKS = (
    "-*,clang-analyzer-*,bugprone-*,cert-*,"
    "-clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling,"
    "-clang-analyzer-optin.performance.Padding,-bugprone-easily-swappable-parameters,"
    "-bugprone-macro-parentheses,-bugprone-reserved-identifier,-cert-dcl37-c,-cert-dcl51-cpp"
)


def production_commands(build):
    database = json.loads((build / "compile_commands.json").read_text(encoding="utf-8"))
    selected = []
    for entry in database:
        source = (Path(entry["directory"]) / entry["file"]).resolve()
        if source.suffix == ".c" and source.is_relative_to(ROOT / "src"):
            selected.append({**entry, "file": str(source)})
    if not selected:
        raise ValueError(f"No production translation units in {build}")
    return selected


def capture(command):
    result = subprocess.run(command, cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            text=True, errors="replace", timeout=180)
    return result.returncode, f"{command}\n{result.stdout}Exit code: {result.returncode}\n"


def analyze(args, report):
    selected = production_commands(args.build_dir)
    tool = args.tool or ("clang-tidy-14" if args.mode == "tidy" else "cppcheck")
    code, version = capture([tool, "--version"])
    report.write(version)
    report.write(f"Compile database: {args.build_dir / 'compile_commands.json'}\n")
    report.write(f"Selected translation units: {len(selected)}\n")
    if code:
        return code
    if args.mode == "tidy" and "version 14." not in version:
        raise ValueError("The CI analyzer requires clang-tidy 14")
    with tempfile.TemporaryDirectory(prefix="xgl-analysis-") as directory:
        scratch = Path(directory)
        if args.mode == "cppcheck":
            database = scratch / "compile_commands.json"
            database.write_text(json.dumps(selected), encoding="utf-8")
            code, output = capture([tool, f"--project={database}", "--std=c11",
                                    "--enable=warning,performance,portability",
                                    "--error-exitcode=1", f"-j{args.parallel}"])
            report.write(output)
            return code

        def analyze_one(task):
            index, entry = task
            context = scratch / str(index)
            context.mkdir()
            (context / "compile_commands.json").write_text(json.dumps([entry]), encoding="utf-8")
            return capture([tool, "-p", str(context), entry["file"], f"--checks={TIDY_CHECKS}",
                            "--warnings-as-errors=*", f"--config-file={ROOT / '.clang-tidy'}"])

        failed = False
        with ThreadPoolExecutor(max_workers=args.parallel) as workers:
            for code, output in workers.map(analyze_one, enumerate(selected)):
                report.write(output)
                failed |= code != 0
        return int(failed)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=("tidy", "cppcheck"))
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--tool")
    parser.add_argument("--parallel", type=int, choices=range(1, 257), default=2, metavar="N")
    parser.add_argument("--report", type=Path, required=True)
    args = parser.parse_args()
    args.build_dir = args.build_dir.resolve()
    args.report.parent.mkdir(parents=True, exist_ok=True)
    with args.report.open("w", encoding="utf-8") as report:
        try:
            result = analyze(args, report)
        except (OSError, KeyError, ValueError, subprocess.SubprocessError) as error:
            report.write(str(error) + "\n")
            result = 1
    print(f"{args.mode} exit {result}, report {args.report}")
    return result


if __name__ == "__main__":
    sys.exit(main())
