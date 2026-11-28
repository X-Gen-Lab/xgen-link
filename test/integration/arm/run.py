"""Build and exercise independently installed Boot SDKs on a Cortex-M0 model."""

import argparse
import hashlib
import json
from pathlib import Path
import platform
import re
import subprocess
import tempfile


FIXTURE = Path(__file__).resolve().parent
SOURCE = FIXTURE.parents[2]


def source_hashes():
    inputs = [SOURCE / "CMakeLists.txt", SOURCE / "CMakePresets.json",
              SOURCE / "dependencies/toolchains.lock.json",
              SOURCE / "test/contracts/sdk/VerifySdkPrefix.cmake"]
    for directory in (SOURCE / "cmake", SOURCE / "include", SOURCE / "src"):
        inputs.extend(path for path in directory.rglob("*") if path.is_file())
    inputs.extend(path for path in FIXTURE.iterdir() if path.is_file())
    return {path.relative_to(SOURCE).as_posix(): hashlib.sha256(path.read_bytes()).hexdigest()
            for path in sorted(inputs)}


class ReferenceRun:
    def __init__(self, output, toolchain, qemu):
        self.output = output
        self.toolchain = toolchain
        self.qemu = qemu
        self.commands = []

    def run(self, *args, timeout=180):
        command = [str(argument) for argument in args]
        log = self.output / f"command-{len(self.commands):02d}.log"
        entry = {"command": command, "cwd": str(SOURCE), "log": log.name}
        self.commands.append(entry)
        try:
            result = subprocess.run(command, cwd=SOURCE,
                                    stdout=subprocess.PIPE,
                                    stderr=subprocess.STDOUT,
                                    check=False, timeout=timeout)
            log.write_bytes(result.stdout)
            entry["exit_code"] = result.returncode
            result.check_returncode()
            return result.stdout.decode("utf-8", errors="replace")
        except subprocess.TimeoutExpired as error:
            log.write_bytes(error.stdout or b"")
            entry["timeout_seconds"] = timeout
            raise
        finally:
            self.write_json("commands.json", self.commands)

    def write_json(self, name, data):
        (self.output / name).write_text(
            json.dumps(data, indent=2) + "\n", encoding="utf-8")

    def arm_tool(self, name):
        return self.toolchain / "bin" / f"arm-none-eabi-{name}"

    def probe(self, elf):
        report = self.run(self.qemu, "-M", "microbit", "-nographic", "-monitor",
                          "none", "-serial", "none", "-semihosting-config",
                          "enable=on,target=native", "-kernel", elf, timeout=30)
        if "xgl-arm-reference: pass" not in report.splitlines():
            raise ValueError("Cortex-M0 consumer did not report a successful exercise")
        return {key: int(value) for key, value in re.findall(
            r"^(\w+)=(\d+)\r?$", report, re.MULTILINE)}

    def measure(self, profile):
        case = self.output / profile
        sdk_build = case / "sdk-build"
        consumer_build = case / "consumer-build"
        stage = case / "install-stage"
        prefix = case / "sdk"
        toolchain_args = ("--toolchain", FIXTURE / "cortex-m0.cmake",
                          f"-DXGL_ARM_TOOLCHAIN_ROOT={self.toolchain}")
        self.run("cmake", "--preset", profile, "-B", sdk_build, *toolchain_args)
        self.run("cmake", "--build", sdk_build, "--parallel", "2")
        self.run("cmake", "--install", sdk_build, "--config", "MinSizeRel",
                 "--prefix", stage)
        stage.rename(prefix)

        configure = ("cmake", "-S", FIXTURE, "-B", consumer_build, "-G", "Ninja",
                     "-DCMAKE_BUILD_TYPE=MinSizeRel", "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
                     f"-DXGL_SDK_PREFIX={prefix}", *toolchain_args)
        self.run(*configure)
        self.run("cmake", "--build", consumer_build, "--parallel", "2")
        elf = consumer_build / "xgl_arm_consumer.elf"
        measured = self.probe(elf)
        required = measured["workspace_required"]
        if required <= 0 or measured["workspace_alignment"] <= 0:
            raise ValueError("Expected positive target workspace size and alignment")

        self.run(*configure, f"-DXGL_REFERENCE_WORKSPACE_BYTES={required}")
        self.run("cmake", "--build", consumer_build, "--parallel", "2")
        final = self.probe(elf)
        if final != {**measured, "workspace_reserved": required}:
            raise ValueError("Exact-size consumer changed the measured configuration")
        profile_settings = (final["window_capacity"], final["adaptive_rto"])
        if profile_settings != ((128, 1) if profile == "boot" else (1, 0)):
            raise ValueError(f"Unexpected installed capacity or RTO for {profile}")

        size_text = self.run(self.arm_tool("size"), "-A", elf)
        sections = {name: {"bytes": int(size), "address": int(address)}
                    for name, size, address in
                    re.findall(r"^(\.\S+)\s+(\d+)\s+(\d+)\s*$", size_text, re.MULTILINE)}
        if not {".isr_vector", ".text", ".bss"}.issubset(sections):
            raise ValueError("Expected Cortex-M0 vectors, code and static RAM in ELF")
        totals = self.run(self.arm_tool("size"), "-B", elf).splitlines()[1].split()
        text, data, bss = map(int, totals[:3])
        symbols = self.run(self.arm_tool("nm"), "-S", "--defined-only", elf)
        workspace = re.search(r"^[\da-f]+\s+([\da-f]+)\s+[bB] s_workspace$", symbols,
                              re.MULTILINE)
        if workspace is None or int(workspace.group(1), 16) != required:
            raise ValueError("Linked workspace size does not match the measured requirement")
        if re.search(r"\s(?:_?(?:m|c|re)alloc(?:_r)?|_?free(?:_r)?)$", symbols,
                     re.MULTILINE):
            raise ValueError("Boot consumer unexpectedly links a libc heap symbol")

        artifacts = {}
        for artifact in (elf, elf.with_suffix(".map")):
            if artifact.stat().st_size == 0:
                raise ValueError(f"Empty artifact: {artifact}")
            artifacts[artifact.suffix[1:]] = {
                "path": str(artifact.relative_to(self.output)),
                "sha256": hashlib.sha256(artifact.read_bytes()).hexdigest(),
            }

        return {"configuration": "MinSizeRel", "workspace": final,
                "sections": sections, "artifacts": artifacts,
                "size_section_bytes": {"flash": text + data, "static_ram": data + bss},
                "stack_frames": {"sdk": self.stack_frames(sdk_build),
                                 "consumer": self.stack_frames(consumer_build)}}

    def stack_frames(self, build):
        frames = []
        for report in sorted(build.rglob("*.su")):
            for line in report.read_text(encoding="utf-8").splitlines():
                function, size, kind = line.rsplit("\t", 2)
                frames.append({"function": function, "bytes": int(size),
                               "kind": kind, "report": str(report.relative_to(self.output))})
        if not frames:
            raise ValueError(f"Missing compiler stack-usage records: {build}")
        return frames


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--toolchain-root", type=Path, required=True)
    parser.add_argument("--work-dir", type=Path, required=True)
    parser.add_argument("--qemu", default="qemu-system-arm")
    args = parser.parse_args()
    args.work_dir.mkdir(parents=True, exist_ok=True)
    output = Path(tempfile.mkdtemp(prefix="run-", dir=args.work_dir.resolve()))
    run = ReferenceRun(output, args.toolchain_root.resolve(), args.qemu)
    print(f"Cortex-M0 evidence: {output}", flush=True)
    try:
        lock = json.loads((SOURCE / "dependencies/toolchains.lock.json").read_text(
            encoding="utf-8"))["arm_gnu"]
        host = f"{platform.machine().lower()}-{platform.system().lower()}"
        version = run.run(run.arm_tool("gcc"), "--version").strip()
        target = run.run(run.arm_tool("gcc"), "-dumpmachine").strip()
        if (host != lock["host"] or target != lock["target"] or
                lock["version"].casefold() not in version.casefold()):
            raise ValueError("Arm GNU toolchain does not match the locked host, target and version")
        provenance = {
            "source_commit": run.run("git", "rev-parse", "HEAD").strip(),
            "source_status": run.run("git", "--no-optional-locks", "status",
                                     "--porcelain").splitlines(),
            "source_files_sha256": source_hashes(),
            "compiler": version, "target": target, "toolchain_lock": lock,
            "qemu": run.run(args.qemu, "--version").strip(),
            "cmake": run.run("cmake", "--version").splitlines()[0],
            "ninja": run.run("ninja", "--version").strip(),
        }
        run.write_json("provenance.json", provenance)
        profiles = {profile: run.measure(profile) for profile in ("boot", "boot-small")}
        if profiles["boot"]["workspace"]["resource_config_id"] == \
                profiles["boot-small"]["workspace"]["resource_config_id"]:
            raise ValueError("Boot Small and Boot unexpectedly have the same identity")
        if source_hashes() != provenance["source_files_sha256"]:
            raise ValueError("Cortex-M0 reference inputs changed during the run")
        run.write_json("report.json", {**provenance, "profiles": profiles,
                                       "emulator": "QEMU microbit",
                                       "physical_execution": "not_executed"})
    except (KeyError, OSError, ValueError, subprocess.SubprocessError) as error:
        parser.exit(1, f"{error}\nSee the command logs under {output}\n")


if __name__ == "__main__":
    main()
