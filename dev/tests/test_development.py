"""Contracts for the explicit, offline development assembly."""

import os
import importlib.util
import io
import json
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock


DEV = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("prepare_dependencies", DEV / "prepare_dependencies.py")
PREPARE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PREPARE)


class DevelopmentEntryTests(unittest.TestCase):
    def test_missing_sources_fail_with_the_explicit_development_variable(self):
        with tempfile.TemporaryDirectory() as directory:
            result = subprocess.run(
                ["cmake", "-S", str(DEV), "-B", directory, "-G", "Ninja"],
                capture_output=True, text=True, check=False)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("XGL_DEV_STATUS_SOURCE_DIR", result.stdout + result.stderr)

    def test_all_five_paths_are_checked_before_component_configuration(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "component"
            source.mkdir()
            marker = root / "configured.txt"
            (source / "CMakeLists.txt").write_text(
                f'file(WRITE "{marker.as_posix()}" "unexpected")\n', "utf-8")
            for missing in PREPARE.COMPONENTS:
                with self.subTest(missing=missing):
                    arguments = ["cmake", "-S", str(DEV), "-B", str(root / missing), "-G", "Ninja"]
                    arguments += [f"-DXGL_DEV_{name.upper()}_SOURCE_DIR={source}"
                                  for name in PREPARE.COMPONENTS if name != missing]
                    result = subprocess.run(arguments, capture_output=True, text=True, check=False)
                    self.assertNotEqual(result.returncode, 0)
                    self.assertIn("XGL_DEV_" + missing.upper() + "_SOURCE_DIR", result.stderr)
                    self.assertFalse(marker.exists())

    def test_bad_absolute_directory_fails_before_compiler_detection(self):
        with tempfile.TemporaryDirectory() as directory:
            result = subprocess.run(
                ["cmake", "-S", str(DEV), "-B", directory, "-G", "Ninja",
                 "-DXGL_DEV_STATUS_SOURCE_DIR=" + str(Path(directory) / "missing")],
                capture_output=True, text=True, check=False)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("XGL_DEV_STATUS_SOURCE_DIR must be an absolute", result.stderr)

    def test_repository_source_must_be_explicit_before_ci_checkout(self):
        environment = dict(os.environ)
        for name in ("STATUS", "BYTES", "CRC", "MEMORY", "CONTAINERS"):
            environment.pop("XGEN_" + name + "_REPOSITORY", None)
        result = subprocess.run(
            [sys.executable, str(DEV / "prepare_dependencies.py"),
             "--manifest", str(DEV / "dependencies.json")],
            env=environment, capture_output=True, text=True, check=False)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("XGEN_STATUS_REPOSITORY", result.stdout + result.stderr)


class CheckoutPreparationTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.manifest = self.root / "dependencies.json"
        self.document = {"schema_version": 1,
                         "components": {name: "a" * 40 for name in PREPARE.COMPONENTS}}
        self.environment = {"XGEN_" + name.upper() + "_REPOSITORY": "Owner/xgen-" + name
                            for name in PREPARE.COMPONENTS}

    def plan(self):
        self.manifest.write_text(json.dumps(self.document), "utf-8")
        return PREPARE.checkout_plan(self.manifest, self.environment, self.root)

    def test_valid_inputs_produce_fixed_paths_without_creating_checkouts(self):
        plan = self.plan()
        self.assertEqual([item["name"] for item in plan], list(PREPARE.COMPONENTS))
        self.assertEqual(plan[0]["revision"], "a" * 40)
        self.assertEqual(plan[0]["path"], (self.root / "out/deps/xgen-status").as_posix())
        self.assertFalse((self.root / "out").exists())

    def test_rejects_unknown_missing_or_unpinned_inputs(self):
        for bad in ("main", "v0.1.0", "a" * 39, 42, "A" * 40):
            with self.subTest(revision=bad):
                self.document["components"]["status"] = bad
                with self.assertRaisesRegex(ValueError, "Pin status"):
                    self.plan()
        self.document["components"]["status"] = "a" * 40
        self.document["components"]["unknown"] = "a" * 40
        with self.assertRaisesRegex(ValueError, "exactly five"):
            self.plan()
        del self.document["components"]["unknown"]
        del self.document["components"]["status"]
        with self.assertRaisesRegex(ValueError, "exactly five"):
            self.plan()

    def test_rejects_wrong_schema_and_repository_injection(self):
        for schema in (True, 2, "1"):
            with self.subTest(schema=schema):
                self.document["schema_version"] = schema
                with self.assertRaisesRegex(ValueError, "schema 1"):
                    self.plan()
        self.document["schema_version"] = 1
        for repository in ("", "owner/repository\ninjected=1", "https://github.com/owner/repo", "a/b/c"):
            with self.subTest(repository=repository):
                self.environment["XGEN_STATUS_REPOSITORY"] = repository
                with self.assertRaisesRegex(ValueError, "XGEN_STATUS_REPOSITORY"):
                    self.plan()

    def test_verify_rejects_wrong_commit_or_dirty_checkout(self):
        repository = self.root / "repo"
        subprocess.run(["git", "init", "-q", str(repository)], check=True)
        (repository / "file.txt").write_text("source\n", "utf-8")
        subprocess.run(["git", "-C", str(repository), "add", "file.txt"], check=True)
        subprocess.run(["git", "-C", str(repository), "-c", "user.name=Test",
                        "-c", "user.email=test@example.invalid", "commit", "-qm", "fixture"], check=True)
        revision = subprocess.check_output(["git", "-C", str(repository), "rev-parse", "HEAD"], text=True).strip()
        plan = [{"name": "status", "path": str(repository), "revision": revision}]
        PREPARE.verify_checkouts(plan)
        plan[0]["revision"] = "b" * 40
        with self.assertRaisesRegex(ValueError, "differs"):
            PREPARE.verify_checkouts(plan)
        plan[0]["revision"] = revision
        (repository / "unexpected.txt").write_text("dirty", "utf-8")
        with self.assertRaisesRegex(ValueError, "must be clean"):
            PREPARE.verify_checkouts(plan)

    def test_cli_outputs_and_verifies_explicit_local_checkouts(self):
        self.plan()
        output = self.root / "outputs.txt"
        environment = self.root / "environment.txt"
        arguments = [str(DEV / "prepare_dependencies.py"), "--manifest", str(self.manifest),
                     "--workspace", str(self.root), "--github-output", str(output), "--github-env", str(environment)]
        with mock.patch.object(sys, "argv", arguments), mock.patch.dict(os.environ, self.environment):
            with redirect_stdout(io.StringIO()) as stdout:
                PREPARE.main()
        self.assertEqual(len(json.loads(stdout.getvalue())), 5)
        self.assertIn("status_revision=" + "a" * 40, output.read_text("utf-8"))
        self.assertIn("XGL_DEV_CONTAINERS_SOURCE_DIR=", environment.read_text("utf-8"))
        self.assertFalse((self.root / "out").exists())

    def test_main_verifies_all_prepared_checkouts_and_rejects_dirty_source(self):
        for name in PREPARE.COMPONENTS:
            source = self.root / "out/deps" / ("xgen-" + name)
            source.mkdir(parents=True)
            subprocess.run(["git", "init", "-q", str(source)], check=True)
            subprocess.run(["git", "-C", str(source), "-c", "user.name=Test",
                            "-c", "user.email=test@example.invalid", "commit", "-qm", "fixture",
                            "--allow-empty"], check=True)
            self.document["components"][name] = subprocess.check_output(
                ["git", "-C", str(source), "rev-parse", "HEAD"], text=True).strip()
        self.plan()
        arguments = [str(DEV / "prepare_dependencies.py"), "--manifest", str(self.manifest),
                     "--workspace", str(self.root), "--verify"]
        with mock.patch.object(sys, "argv", arguments), mock.patch.dict(os.environ, self.environment):
            with redirect_stdout(io.StringIO()) as stdout:
                PREPARE.main()
            self.assertEqual(len(json.loads(stdout.getvalue())), 5)
            (self.root / "out/deps/xgen-memory/untracked.txt").write_text("dirty", "utf-8")
            with redirect_stderr(io.StringIO()) as stderr, self.assertRaises(SystemExit) as raised:
                PREPARE.main()
            self.assertEqual(raised.exception.code, 1)
            self.assertIn("memory: development checkout must be clean", stderr.getvalue())

    def test_cli_reports_invalid_manifest_as_failed_preparation(self):
        self.manifest.write_text("{invalid", "utf-8")
        result = subprocess.run([sys.executable, str(DEV / "prepare_dependencies.py"),
                                 "--manifest", str(self.manifest)], capture_output=True, text=True, check=False)
        self.assertEqual(result.returncode, 1)
        self.assertIn("Development dependencies:", result.stderr)


if __name__ == "__main__":
    unittest.main()
