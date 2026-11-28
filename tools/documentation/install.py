"""Explicitly prepare the Linux CPython 3.12 documentation tools."""

import argparse
from hashlib import file_digest
import json
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import tarfile
import urllib.request
import venv

from check import write_report


ROOT = Path(__file__).resolve().parents[2]
LOCK = ROOT / "dependencies/docs-tools.lock.json"
REQUIREMENTS = ROOT / "dependencies/docs-python-linux-cp312.txt"


def archive_matches(path, lock):
    if not path.is_file() or path.stat().st_size != lock["size"]:
        return False
    with path.open("rb") as stream:
        return file_digest(stream, "sha256").hexdigest() == lock["sha256"]


def prepare(args):
    if (platform.system(), platform.machine(), sys.version_info[:2]) != ("Linux", "x86_64", (3, 12)):
        raise ValueError("Locked installation requires Linux x86_64 and CPython 3.12")
    lock = json.loads(LOCK.read_text(encoding="utf-8"))["doxygen"]
    prefix = args.prefix.resolve()
    cache = args.cache.resolve()
    if prefix.exists() or cache.is_relative_to(prefix):
        raise ValueError("Choose a new installation prefix and an independent cache")
    cache.mkdir(parents=True, exist_ok=True)
    archive = cache / lock["filename"]
    if not archive.exists():
        if not args.allow_network:
            raise ValueError(f"Missing cached Doxygen archive: {archive}")
        incoming = archive.with_suffix(archive.suffix + ".download")
        try:
            with urllib.request.urlopen(lock["url"], timeout=60) as response, incoming.open("wb") as output:
                shutil.copyfileobj(response, output)
            if not archive_matches(incoming, lock):
                raise ValueError("Downloaded Doxygen bytes differ from the lock")
            incoming.replace(archive)
        finally:
            incoming.unlink(missing_ok=True)
    if not archive_matches(archive, lock):
        raise ValueError(f"Cached Doxygen bytes differ from the lock: {archive}")

    prefix.mkdir(parents=True)
    venv.EnvBuilder(with_pip=True).create(prefix / "venv")
    python = prefix / "venv/bin/python"
    wheels = cache / "wheels"
    wheels.mkdir(exist_ok=True)
    pip = [python, "-m", "pip", "--disable-pip-version-check"]
    if args.allow_network:
        subprocess.run([*pip, "download", "--require-hashes", "--only-binary=:all:",
                        "--dest", wheels, "-r", REQUIREMENTS], check=True)
    subprocess.run([*pip, "install", "--require-hashes", "--only-binary=:all:", "--no-index",
                    "--find-links", wheels, "-r", REQUIREMENTS], check=True)
    subprocess.run([*pip, "check"], check=True)

    doxygen = prefix / "bin/doxygen"
    doxygen.parent.mkdir()
    license_path = prefix / "licenses/Doxygen.txt"
    license_path.parent.mkdir()
    with tarfile.open(archive) as bundle:
        for member, destination in ((lock["archive_root"] + "/bin/doxygen", doxygen),
                                    (lock["archive_root"] + "/LICENSE", license_path)):
            source = bundle.extractfile(member)
            if source is None:
                raise ValueError(f"Missing regular Doxygen archive member: {member}")
            with source, destination.open("wb") as output:
                shutil.copyfileobj(source, output)
    doxygen.chmod(0o755)
    doxygen_version = subprocess.check_output([doxygen, "--version"], text=True).strip()
    if doxygen_version.split()[0] != lock["version"]:
        raise ValueError("Doxygen executable does not report the locked version")
    with REQUIREMENTS.open("rb") as stream:
        requirements_sha = file_digest(stream, "sha256").hexdigest()
    with doxygen.open("rb") as stream:
        binary_sha = file_digest(stream, "sha256").hexdigest()
    write_report(prefix / "installation.json", {"python": sys.version, "doxygen": doxygen_version,
                                                 "doxygen_sha256": lock["sha256"],
                                                 "doxygen_binary_sha256": binary_sha,
                                                 "requirements_sha256": requirements_sha})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--prefix", type=Path, required=True)
    parser.add_argument("--cache", type=Path, required=True)
    parser.add_argument("--allow-network", action="store_true")
    args = parser.parse_args()
    try:
        prepare(args)
    except (OSError, ValueError, KeyError, subprocess.CalledProcessError, tarfile.TarError) as error:
        print(f"Documentation tool preparation failed: {error}", file=sys.stderr)
        return error.returncode if isinstance(error, subprocess.CalledProcessError) else 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
