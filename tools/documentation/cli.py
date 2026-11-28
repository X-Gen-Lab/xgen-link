"""Build, preview and check the XGen Link documentation."""

import argparse
from pathlib import Path
import subprocess
import sys

from check import check


ROOT = Path(__file__).resolve().parents[2]


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    actions = parser.add_subparsers(dest="action", required=True)
    check_parser = actions.add_parser("check", help="Check documentation sources offline")
    check_parser.add_argument("--root", type=Path, default=ROOT, help=argparse.SUPPRESS)
    check_parser.add_argument("--staged", action="store_true")
    check_parser.add_argument("--report", type=Path)
    build_parser = actions.add_parser("build", help="Extract gettext and build the site")
    build_parser.add_argument("--mode", choices=("verify", "preview"), default="verify")
    build_parser.add_argument("--language", choices=("all", "zh_CN", "en"), default="all")
    build_parser.add_argument("--output", type=Path, required=True)
    build_parser.add_argument("--tool-prefix", type=Path)
    build_parser.add_argument("--doxygen", default="doxygen")
    build_parser.add_argument("--version", help=argparse.SUPPRESS)
    test_parser = actions.add_parser("test", help="Execute documentation tool regressions")
    test_parser.add_argument("--tool-prefix", type=Path)
    serve_parser = actions.add_parser("serve", help="Serve the last completed site locally")
    serve_parser.add_argument("--build", type=Path, required=True)
    serve_parser.add_argument("--port", type=int, default=8000)
    args = parser.parse_args(argv)
    try:
        if args.action == "check":
            errors = check(args.root.resolve(), staged=args.staged, report=args.report)
            for error in errors:
                print(error, file=sys.stderr)
            return bool(errors)
        if args.action == "build":
            from build import build
            return build(ROOT, args)
        if args.action == "test":
            from build import tool_python
            return subprocess.run([tool_python(args.tool_prefix), "-B", "-m", "unittest",
                                   "discover", "-s", "tools/documentation", "-v"],
                                  cwd=ROOT, check=False).returncode
        site = args.build.resolve() / "site"
        if not (site / "index.html").is_file():
            raise ValueError(f"No completed site in {site}")
        return subprocess.run([sys.executable, "-B", "-m", "http.server", str(args.port),
                               "--bind", "127.0.0.1", "--directory", site], check=False).returncode
    except (ImportError, OSError, ValueError, subprocess.CalledProcessError) as error:
        print(f"Documentation {args.action} failed: {error}", file=sys.stderr)
        return error.returncode if isinstance(error, subprocess.CalledProcessError) else 1


if __name__ == "__main__":
    sys.exit(main())
