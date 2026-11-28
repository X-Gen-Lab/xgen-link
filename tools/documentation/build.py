"""Extract and verify translations, then publish a complete local site."""

from hashlib import file_digest, sha256
from html import escape
from importlib.metadata import version as distribution_version
import json
import os
from pathlib import Path
import posixpath
import re
import shutil
import subprocess
import sys
import tempfile

from check import CATALOG_PREFIX, SOURCE_PREFIX, catalog_identity, check, write_report
from site_contract import verify_site


LANGUAGES = {"zh_CN": "zh", "en": "en"}
SHARED_PAGES = {"index.md": "index.html", "api/index.md": "api/index.html",
                "standards.md": "standards/index.html"}


def tool_python(prefix):
    if prefix is None:
        return Path(sys.executable)
    path = prefix.absolute() / "venv" / ("Scripts/python.exe" if os.name == "nt" else "bin/python")
    if not path.is_file():
        raise ValueError(f"Missing prepared documentation interpreter: {path}")
    return path


def prepared_doxygen(root, prefix, custom):
    if custom != "doxygen":
        raise ValueError("Use the locked Doxygen with --tool-prefix")
    prefix = prefix.absolute()
    receipt = json.loads((prefix / "installation.json").read_text(encoding="utf-8"))
    requirements = root / "dependencies/docs-python-linux-cp312.txt"
    lock = json.loads((root / "dependencies/docs-tools.lock.json").read_text(encoding="utf-8"))
    executable = prefix / "bin/doxygen"
    with executable.open("rb") as stream:
        binary_sha = file_digest(stream, "sha256").hexdigest()
    if (receipt.get("requirements_sha256") != sha256(requirements.read_bytes()).hexdigest()
            or receipt.get("doxygen_sha256") != lock["doxygen"]["sha256"]
            or receipt.get("doxygen_binary_sha256") != binary_sha):
        raise ValueError("Prepared documentation tools do not match the current lock")
    return executable


def command(args, log, *, root, environment):
    result = subprocess.run(args, cwd=root, env=environment, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, check=False)
    log.write_bytes(result.stdout)
    if result.returncode:
        print(result.stdout.decode("utf-8", errors="replace"), file=sys.stderr)
        raise subprocess.CalledProcessError(result.returncode, args)


def source_version(root):
    match = re.search(r"(?m)^set\(XGL_VERSION ([0-9]+\.[0-9]+\.[0-9]+)\)",
                      (root / "cmake/XglVersion.cmake").read_text(encoding="utf-8"))
    if not match:
        raise ValueError("CMake version authority has no XGL_VERSION")
    return match[1]


def extract_api(root, attempt, doxygen, version, environment):
    api = attempt / "doxygen"
    api.mkdir()
    config = attempt / "Doxyfile"
    config.write_text(
        f'@INCLUDE = "{(root / "Doxyfile").as_posix()}"\n'
        f'PROJECT_NUMBER = "{version}"\n'
        f'OUTPUT_DIRECTORY = "{api.as_posix()}"\n'
        f'WARN_LOGFILE = "{(api / "warnings.log").as_posix()}"\n'
        'FULL_PATH_NAMES = NO\nGENERATE_TREEVIEW = YES\nGENERATE_HTML = YES\n',
        encoding="utf-8",
    )
    command([doxygen, config], attempt / "doxygen.log", root=root, environment=environment)
    for target in (api / "xml/index.xml", api / "html/index.html"):
        if not target.is_file() or not target.stat().st_size:
            raise ValueError(f"Missing generated public reference: {target}")
    return api


def prepare_sources(root, stage):
    shutil.copytree(root / SOURCE_PREFIX, stage)
    shutil.copytree(root / "docs/sphinx/diagrams/zh_CN", stage / "diagrams")


def compile_catalogs(root, sources, pot_root, *, strict):
    import polib

    errors = []
    canonical = {path.relative_to(root / SOURCE_PREFIX).with_suffix(".pot")
                 for path in (root / SOURCE_PREFIX).rglob("*.md")}
    extracted = {path.relative_to(pot_root) for path in pot_root.rglob("*.pot")}
    if canonical != extracted:
        raise ValueError(f"Sphinx page/catalog set mismatch: {sorted(canonical ^ extracted)}")
    for pot in sorted(canonical):
        source = (root / SOURCE_PREFIX / pot.with_suffix(".md")).read_bytes()
        author = root / CATALOG_PREFIX / pot.with_suffix(".po")
        target = sources / "locale/en/LC_MESSAGES" / pot.with_suffix(".po")
        try:
            raw = author.read_bytes()
            catalog_identity(raw, source, author.relative_to(root).as_posix())
            draft = polib.pofile(str(author), check_for_duplicates=True)
            expected = {(entry.msgctxt, entry.msgid) for entry in polib.pofile(str(pot_root / pot))}
            translated = {(entry.msgctxt, entry.msgid) for entry in draft if entry.translated()}
            if any(not entry.translated() for entry in draft) or expected != translated:
                raise ValueError(f"{author.relative_to(root)}: catalog differs from extracted messages")
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(author, target)
        except (OSError, ValueError) as error:
            errors.append(str(error))
    if strict and errors:
        raise ValueError("\n".join(errors))
    return errors


def shared_pages(root, site, version, languages):
    from markdown_it import MarkdownIt

    renderer = MarkdownIt("commonmark", {"html": True}).enable("table")
    for source, destination in SHARED_PAGES.items():
        path = site / destination
        path.parent.mkdir(parents=True, exist_ok=True)
        text = (root / "docs" / source).read_text(encoding="utf-8")
        site_base = "https://x-gen-lab.github.io/xgen-link/"
        text = re.sub(r"\]\(" + re.escape(site_base) + r"(en(?:/[^)]*)?/?)\)",
                      lambda entry: "](" + ("../" if "/" in source else "")
                      + entry[1].strip("/") + "/index.md)", text)
        if source in {"index.md", "api/index.md"}:
            for directory, language in (("zh", "zh_CN"), ("en", "en")):
                if language not in languages:
                    text = "\n".join(line for line in text.splitlines()
                                     if f"({directory}/" not in line
                                     and f"(../{directory}/" not in line) + "\n"

        def page_link(match):
            target = posixpath.normpath(posixpath.join(posixpath.dirname(source), match[1]))
            target = target.removesuffix("/index")
            return "](" + posixpath.relpath(target, posixpath.dirname(destination) or ".") + "/)"

        text = re.sub(r"\]\((?!(?:https?://))([^\s)]+)\.md\)",
                      page_link, text)
        title = text.splitlines()[0].removeprefix("# ")
        path.write_text(
            '<!doctype html>\n<html lang="zh-CN"><meta charset="utf-8">\n'
            '<meta name="viewport" content="width=device-width, initial-scale=1">\n'
            f'<title>{escape(title)} | XGen Link</title>\n'
            '<style>body{font:1rem/1.55 system-ui,sans-serif;margin:3rem auto;'
            'padding:0 1rem;max-width:54rem}th,td{padding:.4rem;border:1px solid #aaa}</style>\n'
            f'<main>{renderer.render(text)}</main><footer>XGen Link {version}</footer></html>\n',
            encoding="utf-8",
        )


def publish(output, attempt, site):
    public = output / "site"
    old = attempt / "previous-site"
    if public.exists():
        public.rename(old)
    try:
        site.rename(public)
    except OSError:
        if old.exists():
            old.rename(public)
        raise
    if old.exists():
        shutil.rmtree(old)


def source_digest(root):
    digest = sha256()
    entries = sorted(path for directory in ("docs", "include/xgl", "tools/documentation")
                     for path in (root / directory).rglob("*") if path.is_file()
                     and "__pycache__" not in path.parts)
    entries.extend((root / "Doxyfile", root / "cmake/XglVersion.cmake"))
    entries.extend(sorted(path for path in (root / "dependencies").glob("docs*") if path.is_file()))
    for path in entries:
        digest.update(path.relative_to(root).as_posix().encode("utf-8") + b"\0")
        digest.update(sha256(path.read_bytes()).digest())
    return digest.hexdigest()


def build(root, args):
    python = tool_python(args.tool_prefix)
    if os.path.normcase(str(python.absolute())) != os.path.normcase(sys.executable):
        return subprocess.run([python, "-B", *sys.argv], cwd=root, check=False).returncode
    strict = args.mode == "verify"
    if strict and args.language != "all":
        raise ValueError("Verification requires both languages")
    languages = tuple(LANGUAGES) if args.language == "all" else (args.language,)
    output = args.output.resolve()
    if root.is_relative_to(output) or any(output.is_relative_to(root / name)
                                         for name in ("docs", ".git", "tools", "src", "include")):
        raise ValueError("Documentation output must be separate from authored inputs")
    output.mkdir(parents=True, exist_ok=True)
    attempt = Path(tempfile.mkdtemp(prefix="attempt-", dir=output))
    receipt = output / "documentation-build.json"
    record = {"status": "running", "mode": args.mode, "languages": list(languages),
              "attempt": attempt.name}
    write_report(receipt, record)
    try:
        errors = check(root, allow_unreviewed=not strict, report=attempt / "source-check.json")
        if errors:
            raise ValueError("\n".join(errors))
        release = source_version(root)
        if args.version and args.version != release:
            raise ValueError("CMake and documentation source versions differ")
        input_digest = source_digest(root)
        revision = subprocess.run(["git", "rev-parse", "HEAD"], cwd=root,
                                  capture_output=True, text=True, check=False)
        doxygen = args.doxygen
        if args.tool_prefix:
            doxygen = prepared_doxygen(root, args.tool_prefix, doxygen)
        record.update({"source_commit": revision.stdout.strip() if revision.returncode == 0 else None,
                       "source_sha256": input_digest, "version": release, "python": sys.version,
                       "tools": {name: distribution_version(name) for name in
                                 ("Sphinx", "myst-parser", "breathe", "polib")}})
        environment = dict(os.environ, PYTHONIOENCODING="utf-8", XGL_DOCS_VERSION=release,
                           XGL_DOCS_MODE=args.mode, XGL_DOCS_LANGUAGES=",".join(languages))
        command([doxygen, "--version"], attempt / "doxygen-version.log", root=root,
                environment=environment)
        record["tools"]["Doxygen"] = (attempt / "doxygen-version.log").read_text().strip()
        api = extract_api(root, attempt, doxygen, release, environment)
        sources = attempt / "source"
        prepare_sources(root, sources)
        pots = attempt / "gettext"
        environment["XGL_DOCS_DOXYGEN_XML"] = str(api / "xml")
        sphinx = [python, "-B", "-m", "sphinx", "-W", "--keep-going", "-E", "-c", root / "docs/sphinx"]
        command([*sphinx, "-b", "gettext", "-D", "language=zh_CN", sources, pots],
                attempt / "gettext.log", root=root, environment=environment)
        record["unreviewed_translation_pages"] = compile_catalogs(root, sources, pots, strict=strict)
        site = attempt / "site"
        for language in languages:
            if language == "en":
                shutil.copytree(root / "docs/sphinx/diagrams/en", sources / "diagrams", dirs_exist_ok=True)
            command([*sphinx, "-b", "dirhtml", "-d", attempt / f"doctrees-{language}",
                     "-D", f"language={language}", sources,
                     site / LANGUAGES[language]], attempt / f"sphinx-{language}.log",
                    root=root, environment=environment)
        shared_pages(root, site, release, languages)
        shutil.copytree(api / "html", site / "api/doxygen/html")
        (site / ".nojekyll").touch()
        record["site"] = verify_site(root, site, languages)
        if source_digest(root) != input_digest:
            raise ValueError("Authored documentation inputs changed during the build")
        write_report(site / "source.json", {"source_commit": record["source_commit"],
                                             "source_sha256": input_digest, "version": release,
                                             "mode": args.mode, "languages": list(languages)})
        publish(output, attempt, site)
        record.update(status="passed", exit_code=0)
        print(f"Documentation site: {output / 'site'}")
        return 0
    except (ImportError, OSError, ValueError, subprocess.CalledProcessError) as error:
        record.update(status="failed", error=str(error),
                      exit_code=error.returncode if isinstance(error, subprocess.CalledProcessError) else 1)
        raise
    finally:
        write_report(attempt / "build.json", record)
        write_report(receipt, record)
