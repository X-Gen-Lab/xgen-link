"""Exercise the developer CLI with an isolated Git index and build directory."""

from hashlib import sha256
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time
import unittest


ROOT = Path(__file__).resolve().parents[3]


class DeveloperGate(unittest.TestCase):
    def setUp(self):
        folder = tempfile.TemporaryDirectory(prefix="xgl-dev-fixture-")
        self.addCleanup(folder.cleanup)
        self.root = Path(folder.name)
        self.env = {name: value for name, value in os.environ.items()
                    if not name.startswith("GIT_")}
        self.env.update(GIT_CONFIG_NOSYSTEM="1", GIT_CONFIG_GLOBAL=os.devnull,
                        PYTHONDONTWRITEBYTECODE="1")
        tools = self.root / "tools/dev"
        tools.mkdir(parents=True)
        for name in ("dev.py", "environment.py", "prepare.py"):
            shutil.copyfile(ROOT / "tools/dev" / name, tools / name)
        self.write("CMakePresets.json", json.dumps({
            "version": 3, "configurePresets": [{"name": "native-debug"}],
        }))
        self.write(".gitignore", "/build/\n")
        self.write("README.md", "Developer gate fixture\n")
        self.git("init", "--quiet")
        self.git("config", "core.autocrlf", "false")
        self.git("config", "core.excludesFile", os.devnull)
        self.git("add", ".")
        self.git("-c", "user.name=XGL fixture", "-c", "user.email=fixture@example.invalid",
                 "commit", "-qm", "test: establish isolated developer fixture")
        self.command = [sys.executable, "tools/dev/dev.py"]
        self.build = self.root / "build/native-debug"

    def write(self, name, content):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content, encoding="utf-8", newline="\n")
        return path

    def git(self, *arguments):
        return subprocess.check_output(["git", *arguments], cwd=self.root, env=self.env)

    def check(self):
        return subprocess.run([*self.command, "check", "--staged"], cwd=self.root,
                              env=self.env, capture_output=True, text=True, timeout=15)

    def test_tooling_only_executes_contracts_with_exact_index_fingerprint(self):
        self.write("test/contracts/tooling/test_fixture.py", """\
import unittest

class Fixture(unittest.TestCase):
    def test_tooling_scope_ran(self):
        print("isolated tooling contract executed")
""")
        self.git("add", "test/contracts/tooling/test_fixture.py")
        self.write("README.md", "An unrelated working-tree edit\n")
        observed = set()
        for revision in (1, 2):
            self.write("scripts/ci/example.py", f"TOOL_REVISION = {revision}\n")
            self.git("add", "scripts/ci/example.py")
            index = self.git("ls-files", "--stage", "-z")
            reports = set(self.build.glob("developer-commands/*/source.json"))
            result = self.check()
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("isolated tooling contract executed", result.stdout)
            self.assertEqual(self.git("ls-files", "--stage", "-z"), index)
            created = set(self.build.glob("developer-commands/*/source.json")) - reports
            self.assertEqual(len(created), 1)
            evidence = created.pop()
            self.assertEqual({file.name for file in evidence.parent.iterdir()},
                             {"source.json", "tooling.log"})
            data = json.loads(evidence.read_text(encoding="utf-8"))
            fingerprint = data["staged_index_sha256"]
            self.assertEqual(fingerprint, sha256(index).hexdigest())
            observed.add(fingerprint)
        self.assertEqual(len(observed), 2)

        self.write("scripts/ci/example.py", "TOOL_REVISION = 3\n")
        result = self.check()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Test inputs differ from the index", result.stderr)
        self.assertIn("scripts/ci/example.py", result.stderr)

    @unittest.skipUnless(os.name == "posix", "requires POSIX signals and file locks")
    def test_sigterm_drains_descendant_with_closed_log_pipe(self):
        import fcntl
        import signal

        worker = self.write("worker.py", """\
import fcntl
from pathlib import Path
import signal
import time

signal.signal(signal.SIGTERM, signal.SIG_IGN)
with Path("worker.lock").open("a+b") as lock:
    fcntl.flock(lock, fcntl.LOCK_EX)
    Path("worker.ready").touch()
    deadline = time.monotonic() + 9
    while time.monotonic() < deadline and not Path("worker.release").exists():
        time.sleep(0.02)
""")
        cmake = self.write("bin/cmake", f"""#!{sys.executable}
import subprocess
import sys

worker = subprocess.Popen([sys.executable, {str(worker)!r}],
                          stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
worker.wait()
""")
        cmake.chmod(0o755)
        self.env["PATH"] = str(cmake.parent) + os.pathsep + self.env["PATH"]

        def locked(path):
            with path.open("rb") as handle:
                try:
                    fcntl.flock(handle, fcntl.LOCK_EX | fcntl.LOCK_NB)
                except BlockingIOError:
                    return True
                return False

        with subprocess.Popen([*self.command, "build", "--preset", "native-debug"],
                              cwd=self.root, env=self.env, stdout=subprocess.PIPE,
                              stderr=subprocess.STDOUT, text=True) as process:
            try:
                deadline = time.monotonic() + 6
                while not (self.root / "worker.ready").exists():
                    self.assertIsNone(process.poll(), "managed process exited before worker started")
                    self.assertLess(time.monotonic(), deadline, "worker did not start")
                    time.sleep(0.02)
                self.assertTrue(locked(self.build / ".xgl-build.lock"))
                worker_lock = self.root / "worker.lock"
                self.assertTrue(locked(worker_lock))
                process.send_signal(signal.SIGTERM)
                try:
                    process.wait(timeout=0.5)
                except subprocess.TimeoutExpired:
                    if locked(worker_lock):
                        self.assertTrue(locked(self.build / ".xgl-build.lock"))
                    self.write("worker.release", "")
                output, _ = process.communicate(timeout=7)
                self.assertEqual(process.returncode, 130, output)
                self.assertFalse(locked(worker_lock),
                                 "managed process returned while the descendant still held its lock")
                self.assertFalse(locked(self.build / ".xgl-build.lock"))
                self.assertIn("Exit code: 130", next(self.build.glob(
                    "developer-commands/*/build.log")).read_text(encoding="utf-8"))
            finally:
                self.write("worker.release", "")
                if process.poll() is None:
                    process.kill()


if __name__ == "__main__":
    unittest.main()
