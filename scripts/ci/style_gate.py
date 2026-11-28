#!/usr/bin/env python3
"""Check owned files using the worktree or Git index as a single input."""

import argparse
from fnmatch import fnmatchcase
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import subprocess
import sys
import tempfile


COMMENT_OR_LITERAL = re.compile(
    r'R"(?P<delimiter>[^ ()\\\t\r\n]{0,16})\([\s\S]*?\)(?P=delimiter)"|'
    r'/\*[\s\S]*?\*/|//[^\n]*|"(?:\\[\s\S]|[^"\\])*"|\'(?:\\[\s\S]|[^\'\\])*\''
)
DOXYGEN_TAG = re.compile(r"(?<![\w@])@(?:[A-Za-z]\w*|[{}])")
SOURCE_SUFFIXES = {".c", ".cc", ".cpp", ".cxx"}
HEADER_SUFFIXES = {".h", ".hh", ".hpp", ".hxx"}
TEXT_SUFFIXES = {
    ".c", ".cpp", ".h", ".hpp", ".inc", ".cmake", ".in", ".md", ".py",
    ".yml", ".yaml", ".json", ".txt", ".po", ".rst", ".svg", ".css", ".html", ".js", ".S",
}
TEXT_FILES = {
    ".clang-format", ".clang-format-dirs", ".clang-tidy", ".editorconfig",
    ".gitattributes", ".gersemirc", ".gitignore", "Doxyfile",
}


def git(*args):
    return subprocess.check_output(["git", *args])


def read_input(path, staged):
    return git("show", f":{path}") if staged else Path(path).read_bytes()


def owned_source(path, scope):
    section = False
    included, excluded, extensions = [], [], []
    for line in scope.splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        if line == "[extensions]":
            section = True
        elif section:
            extensions.append(line)
        elif line.startswith("!"):
            excluded.append(line[1:])
        else:
            included.append(line)

    def matches(pattern):
        return fnmatchcase(path, pattern) or fnmatchcase(path, pattern + "/**")

    return (PurePosixPath(path).suffix in extensions and any(map(matches, included))
            and not any(map(matches, excluded)))


def tool(name, env_key):
    repo = Path(__file__).resolve().parents[2]
    scripts = repo / ".venv" / ("Scripts" if os.name == "nt" else "bin")
    selected = os.environ.get(env_key) or shutil.which(name, path=str(scripts)) or shutil.which(name)
    if not selected:
        raise ValueError(f"Missing {name}; run scripts/setup/install_dev_tools.py")
    return selected


def format_tool(name, env_key, expected):
    selected = tool(name, env_key)
    version = subprocess.check_output([selected, "--version"], text=True)
    if not re.search(rf"(?<![.\d]){re.escape(expected)}(?![.\d])", version):
        raise ValueError(f"Expected {name} {expected}; got {version.strip()} at {selected}")
    return selected


def comment_issues(path, text):
    # Lex comments and literals together so strings containing // or @ are safe.
    issues = []
    suffix = PurePosixPath(path).suffix
    has_file_header = False
    for match in COMMENT_OR_LITERAL.finditer(text):
        value = match.group()
        line = text.count("\n", 0, match.start()) + 1
        if not value.startswith(("/*", "//")):
            continue
        if value.startswith("//"):
            issues.append(f"{path}:{line}: Use a C block comment")
        if DOXYGEN_TAG.search(value):
            issues.append(f"{path}:{line}: Use backslash Doxygen tags")
        if re.search(r"(?:^|\n)[ \t]*(?:/\*+|//+|\*)?[ \t]*={3,}", value):
            issues.append(f"{path}:{line}: Use a hyphen section separator")
        if not value.startswith(("/**", "/*!")):
            continue
        tags = set(re.findall(r"(?<!\\)\\([A-Za-z]\w*)", value))
        if suffix in SOURCE_SUFFIXES and tags & {"param", "return", "retval"}:
            issues.append(f"{path}:{line}: Keep API parameter and return tags in the header")
        if "file" in tags and not text[:match.start()].strip():
            has_file_header = True
            required = {"file", "brief", "author"}
            if suffix in SOURCE_SUFFIXES:
                required.update(("version", "date", "copyright"))
            if required - tags:
                issues.append(f"{path}:{line}: Missing file header fields: {', '.join(sorted(required - tags))}")
    if suffix in SOURCE_SUFFIXES | HEADER_SUFFIXES and not has_file_header:
        issues.append(f"{path}:1: Missing the leading Doxygen file header")
    return issues


def check(paths, staged):
    scope = read_input(".clang-format-dirs", staged).decode("utf-8")
    c_paths = [path for path in paths if owned_source(path, scope)]
    cmake_paths = [path for path in paths if path.endswith(".cmake")
                   or PurePosixPath(path).name == "CMakeLists.txt"]
    errors = []
    for path in paths:
        name = PurePosixPath(path)
        if name.suffix not in TEXT_SUFFIXES and name.name not in TEXT_FILES:
            continue
        contents = read_input(path, staged)
        try:
            decoded = contents.decode("utf-8")
        except UnicodeDecodeError:
            errors.append(f"{path}: Expected UTF-8 text")
            continue
        if "\r" in decoded or (decoded and not decoded.endswith("\n")):
            errors.append(f"{path}: Use LF and end the file with a newline")
        if name.suffix != ".md" and any(line.endswith((" ", "\t")) for line in decoded.splitlines()):
            errors.append(f"{path}: Trailing whitespace")
        if path in c_paths:
            errors.extend(comment_issues(path, decoded))
        if path in cmake_paths:
            for number, line in enumerate(decoded.splitlines(), start=1):
                if re.fullmatch(r"\s*#-{30,}", line) and len(line.lstrip()) != 77:
                    errors.append(f"{path}:{number}: CMake section separator must be 77 characters")

    with tempfile.TemporaryDirectory(prefix="xgl-style-") as directory:
        snapshot = Path(directory)
        if c_paths:
            clang = format_tool("clang-format", "XGL_CLANG_FORMAT", "14.0.6")
            style = snapshot / ".clang-format"
            style.write_bytes(read_input(".clang-format", staged))
            for path in c_paths:
                contents = read_input(path, staged)
                formatted = subprocess.check_output(
                    [clang, f"--style=file:{style}", f"--assume-filename={path}"], input=contents,
                )
                if contents != formatted:
                    errors.append(f"{path}: clang-format differs")
        if cmake_paths:
            gersemi = format_tool("gersemi", "XGL_GERSEMI", "0.29.2")
            (snapshot / ".gersemirc").write_bytes(read_input(".gersemirc", staged))
            for path in cmake_paths:
                target = snapshot / path
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(read_input(path, staged))
            result = subprocess.run([gersemi, "--check", *cmake_paths], cwd=snapshot,
                                    text=True, capture_output=True)
            if result.returncode:
                errors.append((result.stdout + result.stderr).strip() or "Gersemi check failed")
    return errors


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    group = parser.add_mutually_exclusive_group()
    group.add_argument("--staged", action="store_true")
    group.add_argument("--all", action="store_true")
    parser.add_argument("--report", type=Path)
    parser.add_argument("files", nargs="*")
    args = parser.parse_args()
    os.chdir(os.fsdecode(git("rev-parse", "--show-toplevel").strip()))
    if args.staged:
        raw_paths = git("diff", "--cached", "--name-only", "--diff-filter=ACMRT", "-z")
    elif args.all:
        raw_paths = git("ls-files", "--cached", "--others", "--exclude-standard", "-z")
    elif args.files:
        raw_paths = git("ls-files", "--cached", "--others", "--exclude-standard", "-z", "--", *args.files)
    else:
        parser.error("choose --staged, --all or paths")
    paths = sorted({os.fsdecode(path) for path in raw_paths.split(b"\0") if path})
    if not args.staged:
        paths = [path for path in paths if Path(path).is_file()]
    try:
        errors = check(paths, args.staged) if paths else []
    except (OSError, subprocess.SubprocessError, ValueError) as error:
        errors = [str(error)]
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps({"staged": args.staged, "files": paths,
                                           "errors": errors}, indent=2) + "\n", encoding="utf-8")
    if errors:
        print("\n".join(errors), file=sys.stderr)
    else:
        print(f"Style checks passed for {len(paths)} paths")
    return int(bool(errors))


if __name__ == "__main__":
    sys.exit(main())
