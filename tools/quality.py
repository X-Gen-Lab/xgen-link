"""Invoke the explicitly installed shared X-Gen quality package."""

from pathlib import Path
import sys

try:
    from xgen_quality import main
except ModuleNotFoundError as error:
    if error.name != "xgen_quality":
        raise
    print("Install the pinned xgen-quality package; see docs/zh/contributing/testing.md.",
          file=sys.stderr)
    raise SystemExit(1) from error

if __name__ == "__main__":
    raise SystemExit(main(root=Path(__file__).resolve().parents[1]))
