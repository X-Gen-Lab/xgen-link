#!/usr/bin/env python3
"""Install the opt-in maintenance environment and standard Git hooks."""

import os
from pathlib import Path
import platform
import subprocess
import sys
import venv


ROOT = Path(__file__).resolve().parents[2]


def main():
    environment = ROOT / ".venv"
    windows = os.name == "nt"
    python = environment / ("Scripts/python.exe" if windows else "bin/python")
    if not python.is_file():
        venv.EnvBuilder(with_pip=True).create(environment)
    command = [python, "-m", "pip", "install", "--only-binary=:all:"]
    if (sys.platform == "linux" and platform.machine() == "x86_64"
            and sys.version_info[:2] == (3, 12)):
        command.extend(("--require-hashes", "-r", "dependencies/python-tools-linux-cp312.txt"))
    else:
        command.extend(("-r", "requirements.txt"))
    subprocess.run(command, cwd=ROOT, check=True)
    subprocess.run([python, "-m", "pre_commit", "install", "--hook-type", "pre-commit",
                    "--hook-type", "commit-msg"], cwd=ROOT, check=True)


if __name__ == "__main__":
    main()
