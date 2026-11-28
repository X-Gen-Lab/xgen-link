"""Test how staged workflow sources are materialized for actionlint."""

import contextlib
import importlib.util
import io
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[3]
SPEC = importlib.util.spec_from_file_location("xgl_workflow_gate", ROOT / "scripts/ci/lint_workflows.py")
WORKFLOW_GATE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(WORKFLOW_GATE)


class StagedWorkflowContract(unittest.TestCase):
    def test_lints_all_staged_workflows_and_local_actions(self):
        with tempfile.TemporaryDirectory(prefix="xgl-workflow-test-") as directory:
            root = Path(directory)
            subprocess.run(["git", "init", "--quiet", root], check=True)
            subprocess.run(["git", "-C", root, "config", "core.autocrlf", "false"], check=True)
            staged = {
                ".github/workflows/first.yml": b"name: First\n",
                ".github/workflows/second.yml": b"name: Second\n",
                ".github/actions/setup/action.yml": b"name: Local action\n",
            }
            for name, contents in {**staged, "dependencies/actionlint.lock.json": b"{}\n"}.items():
                target = root / name
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(contents)
            subprocess.run(["git", "-C", root, "add", "--", "."], check=True)
            (root / ".github/workflows/first.yml").write_bytes(b"name: Unstaged\n")
            (root / ".github/workflows/untracked.yml").write_bytes(b"name: Untracked\n")

            def check_actionlint(command, *, cwd):
                self.assertEqual(command, ["actionlint", "-shellcheck=", "-pyflakes="])
                actual = {path.relative_to(cwd).as_posix(): path.read_bytes()
                          for path in (cwd / ".github").rglob("*.yml")}
                self.assertEqual(actual, staged)
                return 0

            stderr = io.StringIO()
            with (patch.object(WORKFLOW_GATE, "ROOT", root),
                  patch.object(WORKFLOW_GATE, "prepare_actionlint", return_value="actionlint"),
                  patch.object(WORKFLOW_GATE.subprocess, "call", side_effect=check_actionlint) as lint,
                  patch.object(sys, "argv", ["lint_workflows.py", "--staged"]),
                  contextlib.redirect_stderr(stderr)):
                result = WORKFLOW_GATE.main()

            self.assertEqual(result, 0, stderr.getvalue())
            lint.assert_called_once()


if __name__ == "__main__":
    unittest.main()
