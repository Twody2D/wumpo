#!/usr/bin/env python3
"""List the source files a build actually compiles, for linting.

Reading the compilation database instead of globbing the tree means the lint set
can never drift out of sync with the build. It also excludes, for free, sources
a preset does not build - notably the SDL3 backend, which cannot be linted on a
machine without SDL headers.

Usage:
    python tools/list_compiled_sources.py build/ci-core
"""

from __future__ import annotations

import json
import sys
from pathlib import Path

EXCLUDED = ("third_party",)


def main(argv: list[str]) -> int:
    if len(argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2

    database = Path(argv[1]) / "compile_commands.json"
    if not database.is_file():
        print(f"{database} not found; configure the preset first", file=sys.stderr)
        return 1

    entries = json.loads(database.read_text(encoding="utf-8"))
    files = sorted(
        {entry["file"] for entry in entries if not any(part in entry["file"] for part in EXCLUDED)}
    )

    if not files:
        print("compilation database lists no sources", file=sys.stderr)
        return 1

    print("\n".join(files))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
