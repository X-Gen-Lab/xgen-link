"""Check authored documentation and translation identities without dependencies."""

from hashlib import sha256
import json
from pathlib import Path, PurePosixPath
import posixpath
import re
import subprocess
from urllib.parse import unquote, urlsplit
import xml.etree.ElementTree as ET


SOURCE_PREFIX = "docs/zh/"
CATALOG_PREFIX = "docs/locale/en/LC_MESSAGES/"
DOC_LINK = re.compile(r"\[[^\]\n]*\]\(([^)\s]+)(?:\s+[^)]*)?\)")
FRONT_MATTER = re.compile(r"\A---\n(.*?)\n---\n", re.DOTALL)
FIELDS = {"id", "type", "audience", "source_language"}
DOC_TYPES = {"tutorial", "how-to", "explanation", "reference", "landing", "redirect"}
FENCE = re.compile(r"^\s*(`{3,}|~{3,})")


def run_git(root, *args):
    return subprocess.check_output(["git", "-C", root, *args])


def is_document_input(name):
    return ((name.startswith(SOURCE_PREFIX) and name.endswith(".md"))
            or (name.startswith(CATALOG_PREFIX) and name.endswith(".po"))
            or name.startswith("docs/sphinx/")
            or name in {"Doxyfile", "docs/index.md", "docs/api/index.md", "docs/standards.md"})


def inputs(root, staged=False):
    if not staged:
        files = {
            file.relative_to(root).as_posix(): file.read_bytes()
            for name in ("docs",)
            for file in (root / name).rglob("*")
            if file.is_file() and is_document_input(file.relative_to(root).as_posix())
        }
        if (root / "Doxyfile").is_file():
            files["Doxyfile"] = (root / "Doxyfile").read_bytes()
        return files

    records = run_git(root, "ls-files", "--stage", "-z", "--", "docs", "Doxyfile")
    selected = {}
    for entry in records.split(b"\0"):
        if not entry:
            continue
        info, name = entry.split(b"\t", 1)
        path = name.decode("utf-8")
        if not is_document_input(path):
            continue
        mode, identity, stage = info.decode("ascii").split()
        if stage != "0" or mode not in {"100644", "100755"}:
            raise ValueError(f"Unmerged or unsupported staged documentation file: {path}")
        selected[path] = run_git(root, "cat-file", "blob", identity)
    return selected


def catalog_name(name):
    docname = name.removeprefix(SOURCE_PREFIX).removesuffix(".md")
    return f"{CATALOG_PREFIX}{docname}.po"


def metadata(raw, name):
    text = raw.decode("utf-8")
    if "\r" in text or not text.endswith("\n"):
        raise ValueError(f"{name}: expected LF and a final newline")
    front_matter = FRONT_MATTER.match(text)
    if not front_matter:
        raise ValueError(f"{name}: missing page metadata")
    values = {}
    for line in front_matter[1].splitlines():
        key, separator, value = line.partition(": ")
        if not separator or key in values or not value.strip():
            raise ValueError(f"{name}: invalid page metadata: {line}")
        values[key] = value
    if (not FIELDS.issubset(values) or values["source_language"] != "zh_CN"
            or values["type"] not in DOC_TYPES):
        raise ValueError(f"{name}: expected id, type, audience and source_language: zh_CN")
    if values["id"] != name.removeprefix(SOURCE_PREFIX).removesuffix(".md"):
        raise ValueError(f"{name}: id must equal the Sphinx docname")
    return values, text[front_matter.end():]


def catalog_identity(raw, source, name):
    text = raw.decode("utf-8")
    identity = sha256(source).hexdigest()
    if f'"X-XGL-Source-SHA256: {identity}\\n"' not in text:
        raise ValueError(f"{name}: stale source digest")
    if '"Language: en\\n"' not in text or '"X-XGL-Reviewed: true\\n"' not in text:
        raise ValueError(f"{name}: expected an English catalog marked reviewed")
    if any("fuzzy" in line[2:].replace(" ", "").split(",")
           for line in text.splitlines() if line.startswith("#,")):
        raise ValueError(f"{name}: fuzzy message")


def unresolved_links(body, name, sources):
    fence = None
    for number, line in enumerate(body.splitlines(), 1):
        marker = FENCE.match(line)
        if marker:
            marker_character = marker[1][0]
            if fence == marker_character:
                fence = None
            elif fence is None:
                fence = marker_character
            continue
        if fence:
            continue
        for link in DOC_LINK.finditer(line):
            target = urlsplit(link[1].strip("<>"))
            if target.scheme or target.netloc or not target.path.endswith(".md"):
                continue
            path = posixpath.normpath(str(PurePosixPath(name).parent / unquote(target.path)))
            if path not in sources:
                yield f"{name}:{number}: unresolved local Markdown link {link[1]}"


def configuration_errors(files, page_names):
    definitions = {name for name in files if name.startswith("docs/sphinx/diagrams/")}
    for name, raw in sorted(files.items()):
        try:
            if name in definitions:
                svg = ET.fromstring(raw)
                if svg.tag != "{http://www.w3.org/2000/svg}svg" or not svg.get("viewBox"):
                    raise ValueError("missing SVG viewBox")
                relative = name.removeprefix("docs/sphinx/diagrams/")
                language, _, filename = relative.partition("/")
                other = "en" if language == "zh_CN" else "zh_CN"
                if f"docs/sphinx/diagrams/{other}/{filename}" not in definitions:
                    raise ValueError(f"missing {other} diagram variant")
            elif name == "docs/sphinx/conf.py":
                compile(raw, name, "exec")
            elif name == "docs/sphinx/legacy-anchors.json":
                languages = json.loads(raw)
                if set(languages) != {"zh_CN", "en"}:
                    raise ValueError("expected both language keys")
                for aliases in languages.values():
                    for page, names in aliases.items():
                        if f"{SOURCE_PREFIX}{page}.md" not in page_names:
                            raise ValueError(f"historical page route is missing: {page}")
                        if len(names) != len(set(names)):
                            raise ValueError(f"duplicate historical heading ID: {page}")
        except (ET.ParseError, SyntaxError, TypeError, ValueError) as error:
            yield f"{name}: {error}"


def write_report(path, report):
    if path is not None:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def check(root, *, staged=False, allow_unreviewed=False, report=None):
    errors = []
    sources = {}
    try:
        files = inputs(root, staged)
        sources = {name: raw for name, raw in files.items() if name.startswith(SOURCE_PREFIX)}
        if f"{SOURCE_PREFIX}index.md" not in sources:
            errors.append("Canonical documentation has no index.md")
        catalogs = {catalog_name(name) for name in sources}
        for extra in sorted(name for name in files if name.startswith(CATALOG_PREFIX)
                            and name not in catalogs):
            errors.append(f"{extra}: catalog has no canonical page")
        errors.extend(configuration_errors(files, sources))
        for name, raw in sorted(sources.items()):
            try:
                _, body = metadata(raw, name)
                errors.extend(unresolved_links(body, name, sources))
                if allow_unreviewed:
                    continue
                catalog = catalog_name(name)
                if catalog not in files:
                    raise ValueError(f"{catalog}: missing English catalog")
                catalog_identity(files[catalog], raw, catalog)
            except (UnicodeError, ValueError) as error:
                errors.append(str(error))
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        errors.append(str(error))
    write_report(report, {"status": "failed" if errors else "passed", "staged": staged,
                          "pages": len(sources), "errors": errors})
    return errors
