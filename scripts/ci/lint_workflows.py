#!/usr/bin/env python3
"""Run locked actionlint on repository workflow inputs without network access."""

import argparse
import os
from pathlib import Path
import subprocess
import sys
import tempfile

DEV = Path(__file__).resolve().parents[2] / "tools" / "dev"
sys.path.insert(0, str(DEV))
from environment import DEPENDENCIES, ROOT
from prepare import prepare_actionlint


def git_paths(command, *options):
    output = subprocess.check_output(["git", command, "-z", *options], cwd=ROOT)
    return [os.fsdecode(path) for path in output.split(b"\0") if path]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--staged", action="store_true")
    parser.add_argument("--directory", type=Path, default=DEPENDENCIES)
    args = parser.parse_args()
    if args.staged:
        changed = git_paths("diff", "--cached", "--no-renames", "--name-only")
        if not any(path.startswith(".github/") or path == "dependencies/actionlint.lock.json"
                   for path in changed):
            return 0
        staged_lock = subprocess.check_output(["git", "show", ":dependencies/actionlint.lock.json"], cwd=ROOT)
        if staged_lock != (ROOT / "dependencies/actionlint.lock.json").read_bytes():
            print("The actionlint lock in the worktree differs from the index", file=sys.stderr)
            return 1
    try:
        actionlint = prepare_actionlint(args.directory.resolve(), False)
        command = [actionlint, "-shellcheck=", "-pyflakes="]
        if not args.staged:
            return subprocess.call(command, cwd=ROOT)
        with tempfile.TemporaryDirectory(prefix="xgl-workflows-") as directory:
            root = Path(directory)
            subprocess.run(["git", "init", "--quiet", root], check=True)
            for name in git_paths("ls-files", "--cached", "--", ".github"):
                target = root / name
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(subprocess.check_output(["git", "show", f":{name}"], cwd=ROOT))
            return subprocess.call(command, cwd=root)
    except (ValueError, OSError, subprocess.SubprocessError) as error:
        print(error, file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
