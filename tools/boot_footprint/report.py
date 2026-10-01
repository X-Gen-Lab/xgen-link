"""Derive a target workspace layout and audit the linked Boot measurement ELF."""

import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys


def source_identity(root):
    """Record a revision plus source hash so dirty-tree measurements are explicit."""
    root = Path(root).resolve()
    revision = subprocess.run(
        ["git", "-C", str(root), "rev-parse", "--verify", "HEAD"],
        capture_output=True, text=True, check=False)
    status = subprocess.run(
        ["git", "-C", str(root), "status", "--porcelain"],
        capture_output=True, text=True, check=False)
    digest = hashlib.sha256()
    files = []
    for name in ("src", "include", "cmake", "ports", "tools/boot_footprint"):
        directory = root / name
        if directory.exists():
            files.extend(path for path in directory.rglob("*") if path.is_file()
                         and path.suffix in (".c", ".h", ".cmake", ".in", ".ld", ".S", ".py"))
    files.append(root / "CMakeLists.txt")
    for path in sorted(set(files)):
        digest.update(path.relative_to(root).as_posix().encode("utf-8"))
        digest.update(b"\0")
        digest.update(path.read_bytes())
    return {
        "revision": revision.stdout.strip() if revision.returncode == 0 else None,
        "dirty": bool(status.stdout.strip()),
        "source_sha256": digest.hexdigest(),
    }


def cache_values(build):
    """Capture the compiler/toolchain flags used to generate the measured ELF."""
    result = {}
    for line in (Path(build) / "CMakeCache.txt").read_text(encoding="utf-8").splitlines():
        if line.startswith(("//", "#")) or "=" not in line:
            continue
        key, value = line.split("=", 1)
        key = key.split(":", 1)[0]
        if key.startswith(("CMAKE_C_FLAGS", "CMAKE_EXE_LINKER_FLAGS")) or key in (
            "CMAKE_BUILD_TYPE", "CMAKE_TOOLCHAIN_FILE",
            "CMAKE_INTERPROCEDURAL_OPTIMIZATION") or (
                key.startswith(("XGL_", "xgen_")) and key.endswith("_SOURCE_DIR")):
            result[key] = value
    return result


def write_json(path, value):
    Path(path).write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")


def generate_layout(args):
    data = Path(args.input).read_bytes()
    names = (
        "magic", "format_version", "pointer_bytes", "alignment", "workspace_prefix",
        "route_item", "link", "peer", "reliable_packet",
        "instance", "datalink", "transport", "rx_bytes", "frame_bytes", "peers", "routes",
        "links", "window", "tx_packets", "scratch_blocks", "ack_bytes",
        "wire_header_bytes", "wire_crc_bytes",
    )
    if len(data) != len(names) * 4:
        raise ValueError("Unexpected target layout section size")
    values = dict(zip(names, struct.unpack("<" + "I" * len(names), data)))
    if values["magic"] != 0x58474C42 or values["format_version"] != 3:
        raise ValueError("Unsupported target layout format or endianness")
    if any(values[key] != 1 for key in (
            "routes", "peers", "links", "window", "tx_packets", "scratch_blocks")):
        raise ValueError("The footprint layout currently requires one route and peer")
    alignment = values["alignment"]
    if alignment == 0 or alignment & (alignment - 1):
        raise ValueError("Invalid target alignment")

    def align(size):
        return (max(size, values["pointer_bytes"]) + alignment - 1) // alignment * alignment

    initialization = sum(align(values[name]) for name in (
        "route_item", "rx_bytes", "link"))
    payload_size = (values["frame_bytes"] - values["wire_header_bytes"]
                    - values["wire_crc_bytes"])
    partitions = {
        "peer": (values["peer"], values["peers"]),
        "window": (values["ack_bytes"], values["peers"]),
        "tx_packet": (values["reliable_packet"], values["tx_packets"]),
        "tx_payload": (payload_size, values["tx_packets"]),
        "scratch": (values["frame_bytes"], values["scratch_blocks"]),
    }
    runtime_partitions = {
        name: {"block_bytes": align(size), "blocks": count,
               "bytes": align(size) * count}
        for name, (size, count) in partitions.items()
    }
    runtime_bytes = sum(partition["bytes"] for partition in runtime_partitions.values())
    blocks = sum(partition["blocks"] for partition in runtime_partitions.values())
    required = align(values["workspace_prefix"]) + initialization + runtime_bytes
    values.update(initialization_bytes=initialization,
                  runtime_partitions=runtime_partitions,
                  runtime_blocks=blocks, runtime_bytes=runtime_bytes,
                  workspace_bytes=required)
    values["measurement"] = (
        "Target-compiled production type sizes and the fixed Boot workspace partition plan; "
        "the consumer also compares this value with the runtime API if executed."
    )
    write_json(args.output, values)
    Path(args.header).write_text(
        "/* Generated from Cortex-M0 ABI sizes; not a host sizeof estimate. */\n"
        "#ifndef XGL_FOOTPRINT_WORKSPACE_LAYOUT_H\n"
        "#define XGL_FOOTPRINT_WORKSPACE_LAYOUT_H\n"
        f"#define XGL_FOOTPRINT_WORKSPACE_SIZE {required}U\n"
        "#endif\n", encoding="utf-8")
    print(f"Cortex-M0 workspace layout: {required} bytes, alignment {alignment}")


def run(command):
    return subprocess.check_output(command, text=True, encoding="utf-8")


def report_elf(args):
    elf = str(Path(args.elf).resolve())
    size_output = run([args.size, "-B", elf])
    size_lines = [line.split() for line in size_output.splitlines() if line.strip()]
    text_bytes, data_bytes, bss_bytes = map(int, size_lines[-1][:3])
    sections = {}
    for line in run([args.size, "-A", elf]).splitlines():
        fields = line.split()
        if len(fields) >= 3 and fields[0].startswith(".") and fields[1].isdigit():
            sections[fields[0]] = int(fields[1])
    symbols = set()
    for line in run([args.nm, "--defined-only", elf]).splitlines():
        fields = line.split()
        if len(fields) >= 3:
            symbols.add(fields[-1])
    forbidden = sorted(symbols.intersection({
        "malloc", "calloc", "realloc", "free", "_malloc_r", "_calloc_r", "_realloc_r",
        "_free_r", "__malloc_lock", "__malloc_unlock", "_sbrk", "_sbrk_r", "sbrk",
    }))
    unresolved = run([args.nm, "--undefined-only", elf]).strip()
    stack = []
    for path in Path(args.build).rglob("*.su"):
        if "xgl_boot_layout.dir" in str(path):
            continue
        for line in path.read_text(encoding="utf-8").splitlines():
            fields = line.split("\t")
            if len(fields) != 3 or not fields[1].isdigit():
                continue
            function = fields[0].rsplit(":", 1)[-1]
            if function not in symbols:
                continue
            stack.append({"function": function, "bytes": int(fields[1]),
                          "kind": fields[2], "location": fields[0]})
    stack.sort(key=lambda value: value["bytes"], reverse=True)
    reserved_stack = sections.get(".stack", 0)
    layout = json.loads(Path(args.layout).read_text(encoding="utf-8"))
    configuration = cache_values(args.build)
    result = {
        "elf": elf,
        "configuration": f"Cortex-M0, GCC {args.configuration}, boot, no heap, one peer, window 1, MTU 128",
        "compiler": run([args.compiler, "--version"]).splitlines()[0],
        "build_parameters": configuration,
        "link_source": source_identity(Path(__file__).resolve().parents[2]),
        "dependency_sources": {
            key: source_identity(value)
            for key, value in configuration.items()
            if key.startswith("xgen_") and key.endswith("_SOURCE_DIR") and value
        },
        "flash_bytes": text_bytes + data_bytes,
        "text_and_rodata_bytes": text_bytes,
        "initialized_data_bytes": data_bytes,
        "static_ram_bytes": data_bytes + bss_bytes - reserved_stack,
        "reserved_stack_bytes": reserved_stack,
        "ram_with_reserved_stack_bytes": data_bytes + bss_bytes,
        "workspace_bytes": layout["workspace_bytes"],
        "sections": sections,
        "forbidden_heap_symbols": forbidden,
        "unresolved_symbols": unresolved.splitlines() if unresolved else [],
        "largest_linked_function_stack_bytes": stack[0]["bytes"] if stack else None,
        "largest_linked_stack_frames": stack[:20],
        "limits": {
            "generic_flash_64k_pass": text_bytes + data_bytes <= 65536,
            "generic_ram_8k_pass": data_bytes + bss_bytes <= 8192,
            "design_flash_8k_pass": text_bytes + data_bytes <= 8192,
            "design_workspace_1k_pass": layout["workspace_bytes"] <= 1024,
        },
        "qualification": (
            "Linked generic consumer including startup and C library dependencies, not a hardware BSP. "
            "No target execution, interrupt stack, path-summed stack bound, timing or peripheral validation. "
            "Single-function .su sizes do not prove a 512-byte call-chain budget. "
            "Reserved stack is a linker reservation, not measured stack consumption."
        ),
    }
    write_json(args.output, result)
    Path(args.output).with_suffix(".txt").write_text(
        size_output + "\n" + json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(f"Boot ELF: Flash {result['flash_bytes']} B; static RAM {result['static_ram_bytes']} B; "
          f"workspace {result['workspace_bytes']} B; reserved stack {reserved_stack} B")
    print(f"Largest linked function stack: {result['largest_linked_function_stack_bytes']} B")
    if forbidden or unresolved or not (
            result["limits"]["generic_flash_64k_pass"]
            and result["limits"]["generic_ram_8k_pass"]):
        print("ELF audit failed: heap, unresolved symbols, or MCU capacity exceeded",
              file=sys.stderr)
        return 1
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="mode", required=True)
    layout = subparsers.add_parser("layout")
    for name in ("input", "header", "output"):
        layout.add_argument("--" + name, required=True)
    report = subparsers.add_parser("report")
    for name in ("elf", "build", "layout", "size", "nm", "compiler", "configuration", "output"):
        report.add_argument("--" + name, required=True)
    args = parser.parse_args()
    if args.mode == "layout":
        generate_layout(args)
        return 0
    return report_elf(args)


if __name__ == "__main__":
    raise SystemExit(main())
