"""Enforce an explicit product probe budget against a complete ELF report."""

import argparse
import json
from pathlib import Path
import re
import sys


def audit(report, policy):
    """Keep resource limits, historical comparison and MCU claims separate."""
    if policy.get("schema_version") != 1:
        raise ValueError("Unsupported resource policy schema")
    violations = []
    version = policy["compiler_version"]
    compiler = report.get("compiler", "")
    if not re.search(r"(?<![\d.])" + re.escape(version) + r"(?![\d.])", compiler):
        violations.append(f"Compiler does not match fixed version {version}")
    configuration = policy["configuration"]
    actual_profile = report.get("build_parameters", {}).get("XGL_PROFILE")
    if actual_profile != configuration["profile"]:
        violations.append("Report profile does not match the selected policy")
    for field in ("advanced_runtime", "advanced_api_probe"):
        if field in configuration:
            if type(configuration[field]) is not bool:
                raise ValueError(f"Invalid resource policy {field}")
            if type(report.get(field)) is not bool or report[field] != configuration[field]:
                violations.append(f"Report {field} does not match the selected policy")
    for field in ("config_abi_version", "resource_config_id",
                  "compiled_window_capacity", "reliable_bucket_count"):
        if field in configuration:
            expected = configuration[field]
            if type(expected) is not int or expected <= 0:
                raise ValueError(f"Invalid resource policy {field}")
            if type(report.get(field)) is not int or report[field] != expected:
                violations.append(f"Report {field} does not match the selected policy")
    if "adaptive_rto" in configuration:
        if type(configuration["adaptive_rto"]) is not bool:
            raise ValueError("Invalid resource policy adaptive_rto")
        if type(report.get("adaptive_rto")) is not bool or (
                report["adaptive_rto"] != configuration["adaptive_rto"]):
            violations.append("Report adaptive_rto does not match the selected policy")
    for field in ("frame_bytes", "rx_bytes"):
        if field in configuration and report.get(field) != configuration[field]:
            violations.append(f"Report {field} does not match the selected policy")
    if "rx_chunk_bytes" in configuration:
        expected_chunk = configuration["rx_chunk_bytes"]
        if type(expected_chunk) is not int or expected_chunk not in (16, 32, 64, 128):
            raise ValueError("Invalid resource policy rx_chunk_bytes")
        if type(report.get("rx_chunk_bytes")) is not int or report["rx_chunk_bytes"] != expected_chunk:
            violations.append("Report rx_chunk_bytes does not match the selected policy")
    if "lto" in configuration:
        if type(configuration["lto"]) is not bool:
            raise ValueError("Invalid resource policy lto mode")
        if type(report.get("lto")) is not bool or report["lto"] != configuration["lto"]:
            violations.append("Report lto mode does not match the selected policy")
    if "window_pool_blocks" in configuration:
        expected = configuration["window_pool_blocks"]
        if type(expected) is not int or expected != 0:
            raise ValueError("Invalid resource policy window_pool_blocks")
        if type(report.get("window_pool_blocks")) is not int or report["window_pool_blocks"] != expected:
            violations.append("Report window_pool_blocks does not match the selected policy")
    for field in ("forbidden_heap_symbols", "unresolved_symbols"):
        symbols = report.get(field)
        if not isinstance(symbols, list) or symbols:
            violations.append(f"Missing or nonempty {field}")
    observed = {}
    for field, limit in policy["limits"].items():
        value = report.get(field)
        if type(limit) is not int or limit <= 0:
            raise ValueError(f"Invalid resource limit: {field}")
        if type(value) is not int or value < 0:
            violations.append(f"Missing or invalid measurement: {field}")
            continue
        observed[field] = value
        if value > limit:
            violations.append(f"{field}: {value} exceeds {limit}")
    if not observed or not policy["limits"]:
        violations.append("Resource policy did not check any measurements")
    deltas = {}
    for field, baseline in policy["baseline"].items():
        value = report.get(field)
        if type(baseline) is not int or baseline < 0:
            raise ValueError(f"Invalid baseline measurement: {field}")
        if type(value) is int and value >= 0:
            deltas[field] = value - baseline
        else:
            violations.append(f"Missing baseline comparison measurement: {field}")
    return {"schema_version": 1, "status": "failed" if violations else "passed",
            "configuration": configuration, "compiler": compiler,
            "source": report.get("link_source"), "observed": observed,
            "limits": policy["limits"], "baseline": policy["baseline"],
            "deltas": deltas, "violations": violations}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("report", "policy", "output"):
        parser.add_argument("--" + name, type=Path, required=True)
    args = parser.parse_args()
    try:
        result = audit(json.loads(args.report.read_text(encoding="utf-8")),
                       json.loads(args.policy.read_text(encoding="utf-8")))
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    except (OSError, KeyError, TypeError, ValueError) as error:
        print(f"Resource audit failed: {error}", file=sys.stderr)
        return 1
    print(json.dumps(result, indent=2))
    return 0 if result["status"] == "passed" else 1


if __name__ == "__main__":
    raise SystemExit(main())
