"""Sphinx settings for the staged XGen Link documentation sources."""

import json
import os
from pathlib import Path
import sys

from docutils import nodes


_HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(_HERE.parents[1] / "tools/documentation"))
from search_language import XglChineseSearch


project = "XGen Link"
release = os.environ.get("XGL_DOCS_VERSION", "source-preview")
extensions = ["myst_parser", "breathe"]
source_suffix = {".md": "markdown"}
myst_heading_anchors = 6
myst_enable_extensions = ["colon_fence"]
gettext_compact = False
gettext_uuid = False
gettext_location = True
locale_dirs = ["locale/"]
html_theme = "alabaster"
html_title = project
html_show_sourcelink = False
templates_path = [str(_HERE / "templates")]
html_static_path = [str(_HERE / "static")]
html_css_files = ["xgl.css"]
html_sidebars = {"**": ["xgl-language.html", "about.html", "navigation.html", "searchbox.html"]}
html_use_index = False
html_copy_source = False
breathe_projects = {"xgl": os.environ.get("XGL_DOCS_DOXYGEN_XML", "")}
breathe_default_project = "xgl"
breathe_domain_by_extension = {"h": "c"}
breathe_show_include = False


def language_links(app, pagename, template, context, doctree):
    uri = app.builder.get_target_uri(pagename)
    parent = "../" * (uri.count("/") + 1)
    published = os.environ.get("XGL_DOCS_LANGUAGES", "zh_CN,en").split(",")
    context['xgl_chinese'] = parent + "zh/" + uri if "zh_CN" in published else ""
    context['xgl_english'] = parent + "en/" + uri if "en" in published else ""
    context['xgl_languages'] = parent
    context['xgl_source_preview'] = os.environ.get("XGL_DOCS_MODE") == "preview"


def compatibility_anchors(app, doctree, docname):
    anchors = json.loads((_HERE / "legacy-anchors.json").read_text(encoding="utf-8"))
    aliases = anchors.get(app.config.language, {}).get(docname, ())
    used = {identifier for node in doctree.findall(nodes.Element) for identifier in node['ids']}
    for section, identifier in zip(doctree.findall(nodes.section), aliases):
        if identifier and identifier not in used:
            section.insert(0, nodes.target(ids=[identifier]))
            used.add(identifier)


def authored_messages(app, docname, source):
    if app.builder.name == "gettext" and docname == "reference/c-api":
        # The public C comments come directly from the SDK in both HTML views.
        source[0] = source[0].partition("\n```{doxygenindex}")[0] + "\n"


def setup(app):
    app.add_search_language(XglChineseSearch)
    app.connect("html-page-context", language_links)
    app.connect("doctree-resolved", compatibility_anchors)
    app.connect("source-read", authored_messages)
    return {"parallel_read_safe": True, "parallel_write_safe": True}
