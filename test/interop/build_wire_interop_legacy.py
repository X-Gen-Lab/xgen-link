"""Build an interop peer against the pinned SDK 3 without changing SDK 4."""

import argparse
import json
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path
from zipfile import ZipFile


LEGACY_REVISION = "f09efca25abe84c344ce8c41d5645006c7b61df9"
COMPONENTS = ("status", "bytes", "crc", "memory", "containers")
FIXTURE_DIR = Path(__file__).resolve().parent
ROOT = FIXTURE_DIR.parents[1]


def run(*command, capture=False, env=None):
    result = subprocess.run(
        command, check=True, text=True, stdout=subprocess.PIPE if capture else None,
        timeout=300, env=env,
    )
    return result.stdout.strip() if capture else None


def archive_revision(repository, revision, destination):
    resolved = run("git", "-C", repository, "rev-parse", "--verify",
                   f"{revision}^{{commit}}", capture=True)
    if resolved != revision:
        raise RuntimeError(f"Expected {revision} in {repository}, found {resolved}")
    archive = destination.with_suffix(".zip")
    run("git", "-C", repository, "archive", "--format=zip",
        f"--output={archive}", revision)
    with ZipFile(archive) as files:
        files.extractall(destination)


def prepare_component(component, revision, staging, local_dependencies):
    name = f"xgen-{component}"
    if local_dependencies is None:
        repository = staging / f"{name}-git"
        run("git", "init", "--quiet", repository)
        run("git", "-C", repository, "fetch", "--quiet", "--depth=1", "--no-tags",
            f"https://github.com/X-Gen-Lab/{name}.git", revision,
            env={**os.environ, "GIT_TERMINAL_PROMPT": "0"})
    else:
        repository = local_dependencies / name
    source = staging / name
    archive_revision(repository, revision, source)
    print(f"{name} {revision}", flush=True)
    return f"-DXGL_DEV_{component.upper()}_SOURCE_DIR={source}"


def build_peer(staging, profile, local_dependencies):
    legacy = staging / "xgen-link-sdk3"
    archive_revision(ROOT, LEGACY_REVISION, legacy)
    print(f"xgen-link SDK 3 {LEGACY_REVISION}", flush=True)
    manifest = json.loads((legacy / "dev/dependencies.json").read_text(encoding="utf-8"))
    revisions = manifest["components"]
    if manifest["schema_version"] != 1 or set(revisions) != set(COMPONENTS) or any(
        not re.fullmatch(r"[0-9a-f]{40}", revision) for revision in revisions.values()
    ):
        raise RuntimeError("Unexpected dependency manifest in the pinned SDK 3 revision")

    dependency_flags = [
        prepare_component(name, revisions[name], staging, local_dependencies)
        for name in COMPONENTS
    ]
    library_build = staging / "sdk3-build"
    install = staging / "sdk3-install"
    run("cmake", "-S", legacy / "dev", "-B", library_build, "-G", "Ninja",
        "-DCMAKE_BUILD_TYPE=Release", f"-DXGL_PROFILE={profile}",
        "-DXGL_BUILD_TESTS=OFF", "-DXGL_BUILD_EXAMPLES=OFF",
        "-DXGL_BUILD_DOCS=OFF", "-DXGL_BUILD_FOOTPRINT_REPORT=OFF",
        "-DXGL_BUILD_STATIC_ANALYSIS_TARGET=OFF", "-DXGL_BUILD_SDK_CONSUMER_SMOKE=OFF",
        "-DXGL_BUILD_RELEASE_VALIDATION_TARGET=OFF", "-DXGL_BUILD_NOHEAP_SMOKE=OFF",
        "-DXGL_ALLOW_FALLBACK_MALLOC=OFF", *dependency_flags)
    run("cmake", "--build", library_build)
    run("cmake", "--install", library_build, "--prefix", install)

    peer_build = staging / "peer-build"
    run("cmake", "-S", FIXTURE_DIR, "-B", peer_build, "-G", "Ninja",
        "-DCMAKE_BUILD_TYPE=Release", f"-DXGL_PROFILE={profile}",
        f"-DXGL_INTEROP_SDK_PREFIX={install}", "-DXGL_INTEROP_SDK_VERSION=3.0.0")
    run("cmake", "--build", peer_build)
    suffix = ".exe" if os.name == "nt" else ""
    return peer_build / f"xgl_interop_peer{suffix}"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--work-dir", type=Path, required=True)
    parser.add_argument("--profile", choices=("boot", "full"), required=True)
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--deps-dir", type=Path,
                        help="Directory with Git objects for the five pinned foundation sources")
    source.add_argument("--fetch", action="store_true",
                        help="Fetch only the pinned foundation revisions from their public repositories")
    args = parser.parse_args()
    work = args.work_dir.resolve()
    work.mkdir(parents=True, exist_ok=True)
    local_dependencies = args.deps_dir.resolve() if args.deps_dir is not None else None
    try:
        if args.fetch:
            present = subprocess.run(
                ["git", "-C", ROOT, "cat-file", "-e", f"{LEGACY_REVISION}^{{commit}}"],
                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=False, timeout=10,
            )
            if present.returncode != 0:
                run("git", "-C", ROOT, "fetch", "--depth=1", "--no-tags", "origin", LEGACY_REVISION)
        with tempfile.TemporaryDirectory(prefix="legacy-", dir=work) as staging:
            executable = build_peer(Path(staging), args.profile, local_dependencies)
            output = work / f"sdk3-{args.profile}" / executable.name
            output.parent.mkdir(exist_ok=True)
            executable.replace(output)
    except (OSError, RuntimeError, subprocess.SubprocessError) as error:
        print(f"SDK 3 {args.profile} peer preparation failed: {error}", file=sys.stderr)
        return 1
    print(output)
    return 0


if __name__ == "__main__":
    sys.exit(main())
