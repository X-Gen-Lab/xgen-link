"""Verify the public routes and heading aliases in rendered site artifacts."""

from html.parser import HTMLParser
import json
from pathlib import Path


class FragmentIds(HTMLParser):
    def __init__(self):
        super().__init__()
        self.identifiers = set()

    def handle_starttag(self, tag, attributes):
        self.identifiers.update(value for key, value in attributes if key == "id")


def text_at(path):
    if not path.is_file() or not path.stat().st_size:
        raise ValueError(f"Missing public documentation artifact: {path}")
    return path.read_text(encoding="utf-8")


def verify_site(root, site, languages):
    language_paths = {"zh_CN": "zh", "en": "en"}
    legacy = json.loads((root / "docs/sphinx/legacy-anchors.json").read_text(encoding="utf-8"))
    docnames = {path.relative_to(root / "docs/zh").with_suffix("").as_posix()
                for path in (root / "docs/zh").rglob("*.md")}
    stats = {}
    for language in languages:
        destination = site / language_paths[language]
        aliases = 0
        for docname in docnames:
            page = destination / docname if docname == "index" or docname.endswith("/index") else (
                destination / docname / "index")
            page = page.with_suffix(".html")
            html = text_at(page)
            parser = FragmentIds()
            parser.feed(html)
            expected = set(legacy[language].get(docname, ()))
            missing = expected - parser.identifiers
            if missing:
                raise ValueError(f"{page.relative_to(site)}: missing legacy fragments {sorted(missing)}")
            aliases += len(expected)
            if docname == "reference/c-api" and "c.xgl_init" not in parser.identifiers:
                raise ValueError(f"{page.relative_to(site)}: generated C API is missing definitions")
        index = text_at(destination / "searchindex.js")
        if language == "zh_CN" and "xglcjk_" not in index:
            raise ValueError("Chinese search index has no Han search terms")
        text_at(destination / "_static/searchtools.js")
        stats[language] = {"pages": len(docnames), "legacy_fragments": aliases}
    for path in ("index.html", "api/index.html", "api/doxygen/html/index.html", "standards/index.html"):
        text_at(site / Path(path))
    return stats
