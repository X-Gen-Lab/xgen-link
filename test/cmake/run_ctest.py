"""Run all tests in a CMake build and verify the recorded executions."""

import argparse
from collections import Counter
import json
from pathlib import Path
import subprocess
import sys
import xml.etree.ElementTree as ET


def run(build, configuration):
    reports = build
    cache = build / "CMakeCache.txt"
    if cache.is_file():
        configurations = next((line.partition("=")[2]
                               for line in cache.read_text(encoding="utf-8").splitlines()
                               if line.startswith("CMAKE_CONFIGURATION_TYPES:")), "")
        if configurations:
            selected = next((value for value in configurations.split(";")
                             if value.casefold() == configuration.casefold()), None)
            if selected is None:
                raise ValueError(f"Unknown configuration {configuration!r}; choose {configurations}")
            reports = build / "test-results" / selected
            reports.mkdir(parents=True, exist_ok=True)

    inventory_path = reports / "ctest-inventory.json"
    junit = reports / "ctest.xml"
    inventory_path.unlink(missing_ok=True)
    junit.unlink(missing_ok=True)

    command = ["ctest", "--test-dir", build, "-C", configuration]
    inventory = subprocess.check_output(command + ["--show-only=json-v1"])
    inventory_path.write_bytes(inventory)
    tests = json.loads(inventory)["tests"]
    expected = Counter(test["name"] for test in tests)
    if not expected or len(expected) != len(tests):
        raise ValueError("Expected a nonempty inventory of unique CTest names")

    result = subprocess.run(command + ["--no-tests=error", "--output-on-failure",
                                       "--output-junit", junit], check=False)
    if result.returncode != 0:
        return result.returncode

    cases = ET.parse(junit).getroot().findall(".//testcase")
    if Counter(case.get("name") for case in cases) != expected:
        raise ValueError("CTest inventory and JUnit results differ")
    failure_tags = {"skipped", "failure", "error"}
    for case in cases:
        if case.get("status") != "run" or any(child.tag in failure_tags for child in case):
            raise ValueError(f"CTest case did not pass: {case.get('name')}")
    print(f"Verified {len(cases)} CTest executions")
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--config", required=True)
    options = parser.parse_args()
    try:
        return run(options.build_dir.resolve(), options.config)
    except subprocess.CalledProcessError as error:
        print(f"CTest discovery failed: {error}", file=sys.stderr)
        return error.returncode
    except (OSError, ValueError, KeyError, ET.ParseError) as error:
        print(f"CTest validation failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
