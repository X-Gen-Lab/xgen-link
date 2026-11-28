"""Exercise the actual hook against a partially staged CMake/C worktree."""

import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[3]
GATE = ROOT / "scripts" / "ci" / "style_gate.py"


class StagedStyle(unittest.TestCase):
    def test_reads_index_and_handles_rename_and_delete(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            env = {name: value for name, value in os.environ.items()
                   if not name.startswith("GIT_")}
            env.update(GIT_CONFIG_NOSYSTEM="1", GIT_CONFIG_GLOBAL=os.devnull)

            def git(*options):
                subprocess.run(["git", *options], cwd=root, env=env, check=True,
                               stdout=subprocess.DEVNULL)

            git("init", "--quiet")
            git("config", "core.autocrlf", "false")
            git("config", "core.excludesFile", os.devnull)
            for name in (".clang-format", ".clang-format-dirs", ".editorconfig"):
                (root / name).write_bytes((ROOT / name).read_bytes())
            source = root / "src"
            source.mkdir()
            header = (b"/**\n * \\file            old.h\n"
                      b" * \\brief           Declaration.\n"
                      b" * \\author          X-Gen Lab\n */\n\nint run(void);\n")
            (source / "old.h").write_bytes(header)
            (source / "retired.h").write_bytes(header)
            git("add", ".")
            git("-c", "user.name=XGL fixture", "-c", "user.email=fixture@example.invalid",
                "commit", "-qm", "test: prepare staged fixture")
            git("mv", "src/old.h", "src/new.h")
            git("rm", "-q", "src/retired.h")
            target = source / "new.h"
            valid = header.replace(b"old.h", b"new.h")
            target.write_bytes(valid)
            git("add", "src/new.h")
            invalid = valid.replace(b"int run(void);", b"int     run(void);")
            target.write_bytes(invalid)

            def check():
                return subprocess.run([sys.executable, str(GATE), "--staged"],
                                      cwd=root, env=env, text=True,
                                      stdout=subprocess.PIPE, stderr=subprocess.STDOUT)

            result = check()
            self.assertEqual(result.returncode, 0, result.stdout)
            self.assertEqual(target.read_bytes(), invalid)
            git("add", "src/new.h")
            target.write_bytes(valid)
            result = check()
            self.assertNotEqual(result.returncode, 0, result.stdout)
            self.assertIn("src/new.h", result.stdout)
            self.assertEqual(target.read_bytes(), valid)


if __name__ == "__main__":
    unittest.main()
