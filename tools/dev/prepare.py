"""Prepare exact build inputs before CMake configuration."""

import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import tarfile
import tempfile
from urllib.parse import urlparse
from urllib.request import urlopen
import zipfile

from environment import ROOT, build_lock


def read_lock(name):
    return json.loads((ROOT / "dependencies" / name).read_text(encoding="utf-8"))


def sha256_file(path):
    digest = hashlib.sha256()
    with path.open("rb") as contents:
        for chunk in iter(lambda: contents.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def obtain_archive(spec, directory, network):
    archive = directory / Path(urlparse(spec["url"]).path).name
    if archive.is_file() and sha256_file(archive) == spec["sha256"]:
        return archive
    if not network:
        raise ValueError(f"Missing verified {archive.name}; rerun prepare with --allow-network")
    with tempfile.TemporaryDirectory(dir=directory, prefix="download-") as temporary:
        download = Path(temporary) / archive.name
        with urlopen(spec["url"], timeout=90) as source, download.open("wb") as output:
            shutil.copyfileobj(source, output)
        if sha256_file(download) != spec["sha256"]:
            raise ValueError(f"Archive SHA-256 mismatch: {archive.name}")
        download.replace(archive)
    return archive


def actionlint_asset(lock):
    machine = {"x86_64": "amd64", "AMD64": "amd64", "aarch64": "arm64"}.get(
        platform.machine(), platform.machine()
    )
    key = f"{platform.system().lower()}_{machine}"
    if key not in lock["assets"]:
        raise ValueError(f"No locked actionlint distribution for {key}")
    return lock["assets"][key]


def prepare_actionlint(directory, network):
    lock = read_lock("actionlint.lock.json")
    asset = actionlint_asset(lock)
    target = directory / "actionlint" / lock["version"]
    members = [asset["executable"], asset["notice"]]
    with build_lock(target):
        if not all((target / member["member"]).is_file() and
                   sha256_file(target / member["member"]) == member["sha256"] for member in members):
            archive = obtain_archive(asset, target, network)
            if asset["format"] == "zip":
                with zipfile.ZipFile(archive) as packed:
                    contents = [packed.read(member["member"]) for member in members]
            else:
                with tarfile.open(archive) as packed:
                    contents = [packed.extractfile(member["member"]).read() for member in members]
            for member, data in zip(members, contents):
                if hashlib.sha256(data).hexdigest() != member["sha256"]:
                    raise ValueError(f"Invalid archive member: {member['member']}")
                output = target / member["member"]
                output.write_bytes(data)
                output.chmod(member["mode"])
    return target / asset["executable"]["member"]


def prepare_arm(directory, network):
    spec = read_lock("toolchains.lock.json")["arm_gnu"]
    if platform.system() != "Linux" or platform.machine() != "x86_64":
        raise ValueError("The locked ARM reference toolchain uses Linux x86_64")
    with build_lock(directory):
        archive = obtain_archive(spec, directory, network)
        target = directory / spec["archive_root"]
        marker = target / ".xgl-archive-sha256"
        if not marker.is_file() or marker.read_text().strip() != spec["sha256"]:
            with tempfile.TemporaryDirectory(dir=directory, prefix="arm-") as temporary:
                subprocess.run(["tar", "-xf", archive, "-C", temporary], check=True)
                prepared = Path(temporary) / spec["archive_root"]
                (prepared / marker.name).write_text(spec["sha256"] + "\n", encoding="ascii")
                if target.exists():
                    target.rename(Path(temporary) / "previous-install")
                prepared.replace(target)
        subprocess.run([target / "bin" / "arm-none-eabi-gcc", "--version"],
                       stdout=sys.stderr, check=True)
    return target


def native_compilers():
    default = "cl" if os.name == "nt" and os.getenv("VCToolsVersion") else (
        "g++" if os.name == "nt" else "c++"
    )
    compiler = shutil.which(os.environ.get("CXX", default))
    if not compiler:
        raise ValueError("A configured native C++ compiler is required to prepare GoogleTest")
    c_default = {"g++": "gcc", "clang++": "clang", "cl": "cl", "c++": "cc"}.get(default, "cc")
    c_names = (("g++", "gcc"), ("clang++", "clang"), ("c++", "cc"))
    c_path = next((str(Path(compiler).with_name(Path(compiler).name.replace(cxx, c, 1)))
                   for cxx, c in c_names if cxx in Path(compiler).name), c_default)
    c_compiler = shutil.which(os.environ.get("CC", c_path))
    if not c_compiler:
        raise ValueError("Set CC to the C compiler paired with CXX before preparing GoogleTest")
    return c_compiler, compiler


def prepare_gtest(directory, network, configuration):
    lock = read_lock("googletest.lock.json")
    c_compiler, compiler = native_compilers()
    if Path(compiler).stem.lower() == "cl":
        identity = (os.environ.get("VCToolsVersion", ""), os.environ.get("VSCMD_ARG_TGT_ARCH", ""),
                    "shared-crt")
    else:
        identity = tuple(subprocess.check_output([compiler, switch], text=True).strip()
                         for switch in ("--version", "-dumpmachine"))
    key = hashlib.sha256(repr((lock, platform.platform(), compiler, c_compiler,
                               identity, configuration)).encode()).hexdigest()[:24]
    target = directory / "googletest" / key
    with build_lock(target):
        package = sorted((target / "install").rglob("GTestConfig.cmake"))
        if len(package) == 1 and (target / "ready").is_file():
            version_file = package[0].with_name("GTestConfigVersion.cmake")
            if version_file.is_file() and f'"{lock["package_version"]}"' in version_file.read_text():
                return package[0].parent
        if not network:
            raise ValueError("GoogleTest is not prepared for this compiler and configuration; use --allow-network")
        with tempfile.TemporaryDirectory(dir=target, prefix="prepare-") as temporary:
            tree = Path(temporary)
            source, build, install = tree / "source", tree / "build", tree / "install"
            subprocess.run(["git", "init", "--quiet", source], stdout=sys.stderr, check=True)
            subprocess.run(["git", "-C", source, "fetch", "--depth", "1", lock["url"], lock["revision"]],
                           stdout=sys.stderr, check=True)
            subprocess.run(["git", "-C", source, "checkout", "--quiet", "--detach", "FETCH_HEAD"],
                           stdout=sys.stderr, check=True)
            revision = subprocess.check_output(["git", "-C", source, "rev-parse", "HEAD"], text=True).strip()
            if revision != lock["revision"]:
                raise ValueError("GoogleTest checkout does not match the locked revision")
            configure = ["cmake", "-S", source, "-B", build, "-G", "Ninja",
                         f"-DCMAKE_CXX_COMPILER={compiler}", f"-DCMAKE_C_COMPILER={c_compiler}",
                         f"-DCMAKE_BUILD_TYPE={configuration}",
                         "-DBUILD_GMOCK=ON", "-DINSTALL_GTEST=ON", "-Dgtest_force_shared_crt=ON"]
            subprocess.run(configure, stdout=sys.stderr, check=True)
            subprocess.run(["cmake", "--build", build, "--parallel", "2"], stdout=sys.stderr, check=True)
            subprocess.run(["cmake", "--install", build, "--prefix", install], stdout=sys.stderr, check=True)
            package = sorted(install.rglob("GTestConfig.cmake"))
            if len(package) != 1:
                raise ValueError("GoogleTest install must contain a unique GTestConfig.cmake")
            relative = package[0].parent.relative_to(install)
            existing = target / "install"
            if existing.exists():
                existing.rename(tree / "previous-install")
            install.replace(existing)
            (target / "ready").write_text(revision + "\n", encoding="ascii")
    return target / "install" / relative
