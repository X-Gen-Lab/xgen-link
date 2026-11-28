#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# MIT License
#
# Copyright (c) 2026 Nexus Embedded Platform Team
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.
"""Check the Conventional Commit header used by the contribution guides."""

import argparse
from pathlib import Path
import re
import sys

HEADER = re.compile(
    r"^(feat|fix|docs|style|refactor|perf|test|build|ci|chore|revert)"
    r"(?:\([A-Za-z0-9_./-]+\))?!?: \S.*$"
)
PULL_REQUEST_MERGE = re.compile(
    r"Merge pull request #[1-9][0-9]* from [A-Za-z0-9_.-]+/\S+"
)


def valid_message(message):
    """Accept conventional headers and Git-generated merge/revert messages."""
    lines = message.splitlines()
    if not lines:
        return False
    header = lines[0]
    return bool(HEADER.fullmatch(header) or PULL_REQUEST_MERGE.fullmatch(header)
                or header.startswith(
                    ("Merge branch '", "Merge remote-tracking branch '", 'Revert "'))
    )


def main(arguments=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("message_file", type=Path)
    args = parser.parse_args(arguments)
    try:
        message = args.message_file.read_text(encoding="utf-8")
        if not valid_message(message):
            print(
                "Commit header must follow <type>(<scope>): <subject>, "
                "for example: fix(transport): retain the pending ACK on retry",
                file=sys.stderr,
            )
            return 1
        return 0
    except (OSError, UnicodeError) as error:
        print(f"Cannot check commit message: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
