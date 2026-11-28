"""Shared paths and exclusive ownership for managed build directories."""

from contextlib import contextmanager, suppress
import os
from pathlib import Path
import signal
import subprocess
import sys
import time


ROOT = Path(__file__).resolve().parents[2]
DEPENDENCIES = ROOT / "build" / "dependencies"


def use_maintenance_tools():
    scripts = ROOT / ".venv" / ("Scripts" if os.name == "nt" else "bin")
    if scripts.is_dir():
        os.environ["PATH"] = str(scripts) + os.pathsep + os.environ["PATH"]


@contextmanager
def build_lock(directory):
    directory.mkdir(parents=True, exist_ok=True)
    with (directory / ".xgl-build.lock").open("a+b") as lock:
        lock.seek(0)
        try:
            if os.name == "nt":
                import msvcrt

                msvcrt.locking(lock.fileno(), msvcrt.LK_NBLCK, 1)
            else:
                import fcntl

                fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except OSError as error:
            raise ValueError(f"Build directory is already in use: {directory}") from error
        yield


def run_logged(arguments, log, *, cwd=ROOT):
    log.parent.mkdir(parents=True, exist_ok=True)
    command = [str(argument) for argument in arguments]
    with log.open("w", encoding="utf-8", errors="replace") as report:
        print(command, file=report, flush=True)
        with subprocess.Popen(command, cwd=cwd, stdout=subprocess.PIPE,
                              stderr=subprocess.STDOUT, text=True, errors="replace",
                              start_new_session=os.name != "nt",
                              creationflags=subprocess.CREATE_NEW_PROCESS_GROUP if os.name == "nt" else 0) as process:
            try:
                for line in process.stdout:
                    print(line, end="", flush=True)
                    report.write(line)
                code = process.wait()
            except KeyboardInterrupt:
                report.flush()
                if os.name == "nt":
                    subprocess.run(["taskkill", "/PID", str(process.pid), "/T", "/F"],
                                   stdout=report, stderr=report, check=False)
                else:
                    deadline = time.monotonic() + 5
                    with suppress(ProcessLookupError):
                        os.killpg(process.pid, signal.SIGTERM)
                try:
                    remaining, _ = process.communicate(timeout=5)
                except subprocess.TimeoutExpired:
                    if os.name == "nt":
                        process.kill()
                    else:
                        with suppress(ProcessLookupError):
                            os.killpg(process.pid, signal.SIGKILL)
                    remaining, _ = process.communicate()
                if os.name != "nt":
                    # Detached output pipes can close before every build worker exits.
                    with suppress(ProcessLookupError):
                        while time.monotonic() < deadline:
                            os.killpg(process.pid, 0)
                            time.sleep(0.05)
                        os.killpg(process.pid, signal.SIGKILL)
                report.write(remaining or "")
                code = 130
        print(f"Exit code: {code}", file=report)
    if code:
        raise subprocess.CalledProcessError(code, command)


def python_command(path, *arguments):
    return [sys.executable, str(ROOT / path), *arguments]
