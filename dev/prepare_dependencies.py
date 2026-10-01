"""Validate explicit CI checkout inputs; never download or select versions."""

import argparse
import json
import os
from pathlib import Path
import re
import subprocess


COMPONENTS = ("status", "bytes", "crc", "memory", "containers")


def checkout_plan(manifest, environment, workspace):
    data = json.loads(Path(manifest).read_text("utf-8"))
    if (not isinstance(data, dict) or set(data) != {"schema_version", "components"} or
            type(data["schema_version"]) is not int or data["schema_version"] != 1 or
            not isinstance(data["components"], dict) or
            set(data["components"]) != set(COMPONENTS)):
        raise ValueError("Expected schema 1 and exactly five explicit development components")
    plan = []
    for name in COMPONENTS:
        variable = "XGEN_" + name.upper() + "_REPOSITORY"
        repository = environment.get(variable, "")
        if not re.fullmatch(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+", repository):
            raise ValueError(f"Set {variable} explicitly to the published owner/repository")
        revision = data["components"][name]
        if not isinstance(revision, str) or not re.fullmatch(r"[0-9a-f]{40}", revision):
            raise ValueError(f"Pin {name} to a full 40-character lowercase Git commit")
        plan.append({"name": name, "repository": repository, "revision": revision,
                     "path": (Path(workspace).resolve() / "out/deps" / ("xgen-" + name)).as_posix()})
    return plan


def verify_checkouts(plan):
    for item in plan:
        def git(*arguments):
            result = subprocess.run(["git", "-C", item["path"], *arguments],
                                    capture_output=True, text=True, check=True)
            return result.stdout.strip()
        if git("rev-parse", "HEAD") != item["revision"]:
            raise ValueError(f"{item['name']}: checkout differs from the development commit")
        if git("status", "--porcelain", "--untracked-files=normal"):
            raise ValueError(f"{item['name']}: development checkout must be clean")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--workspace", type=Path, default=Path.cwd())
    parser.add_argument("--github-output", type=Path)
    parser.add_argument("--github-env", type=Path)
    parser.add_argument("--verify", action="store_true")
    args = parser.parse_args()
    try:
        plan = checkout_plan(args.manifest, os.environ, args.workspace)
        if args.verify:
            verify_checkouts(plan)
        if args.github_output:
            with args.github_output.open("a", encoding="utf-8", newline="\n") as output:
                for item in plan:
                    for field in ("repository", "revision"):
                        output.write(f"{item['name']}_{field}={item[field]}\n")
        if args.github_env:
            with args.github_env.open("a", encoding="utf-8", newline="\n") as output:
                for item in plan:
                    output.write(f"XGL_DEV_{item['name'].upper()}_SOURCE_DIR={item['path']}\n")
        print(json.dumps(plan, indent=2))
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"Development dependencies: {error}\n")


if __name__ == "__main__":
    main()
