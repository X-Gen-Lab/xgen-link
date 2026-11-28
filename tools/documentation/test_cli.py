"""Exercise the documentation CLI without network access or repository writes."""

from hashlib import sha256
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
CLI = ROOT / "tools/documentation/cli.py"


class DocumentationChecks(unittest.TestCase):
    def git(self, *args):
        return subprocess.check_output(["git", "-C", self.root, *args])

    def setUp(self):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.root = Path(directory.name)
        self.git("init", "-q")
        self.git("config", "core.autocrlf", "false")
        self.source = self.root / "docs/zh/index.md"
        self.source.parent.mkdir(parents=True)
        self.source.write_text(
            "---\nid: index\ntype: how-to\naudience: user\n"
            "source_language: zh_CN\n---\n\n# XGL\n", encoding="utf-8", newline="\n",
        )
        self.catalog = self.root / "docs/locale/en/LC_MESSAGES/index.po"
        self.catalog.parent.mkdir(parents=True)
        self.write_catalog()
        self.git("add", "--", "docs")

    def write_catalog(self):
        identity = sha256(self.source.read_bytes()).hexdigest()
        self.catalog.write_text('msgid ""\nmsgstr ""\n"Language: en\\n"\n'
                                f'"X-XGL-Source-SHA256: {identity}\\n"\n'
                                '"X-XGL-Reviewed: true\\n"\n', encoding="utf-8", newline="\n")

    def check(self, *args):
        return subprocess.run(
            [sys.executable, "-B", CLI, "check", "--root", self.root, *args],
            capture_output=True, text=True,
        )

    def test_reads_index_instead_of_unstaged_content(self):
        self.source.write_text("# Missing metadata\n", encoding="utf-8")
        result = self.check("--staged")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertNotEqual(self.check().returncode, 0)

    def test_staged_broken_link_fails_with_current_report(self):
        with self.source.open("a", encoding="utf-8", newline="\n") as stream:
            stream.write("\n[Missing](missing.md)\n")
        self.git("add", "--", "docs/zh/index.md")
        report = self.root / "check.json"
        result = self.check("--staged", "--report", report)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("missing.md", result.stderr)
        self.assertEqual(json.loads(report.read_text(encoding="utf-8"))["status"],
                         "failed")

    def test_stale_catalog_is_rejected(self):
        with self.source.open("a", encoding="utf-8", newline="\n") as stream:
            stream.write("\nMore information.\n")
        self.git("add", "--", "docs/zh/index.md")
        self.assertIn("stale source digest", self.check("--staged").stderr)

    def test_resolves_links_against_staged_renames(self):
        other = self.root / "docs/zh/other.md"
        other.write_text(self.source.read_text(encoding="utf-8").replace("id: index", "id: other"),
                         encoding="utf-8", newline="\n")
        with self.source.open("a", encoding="utf-8", newline="\n") as stream:
            stream.write("\n[Other](other.md)\n")
        self.git("add", "--", "docs")
        self.git("mv", "docs/zh/other.md", "docs/zh/moved.md")
        result = self.check("--staged")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("unresolved local Markdown link other.md", result.stderr)


if __name__ == "__main__":
    unittest.main()
