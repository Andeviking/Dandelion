#!/usr/bin/env python3
"""Expose the source-discovery result to the GoogleTest tooling tests."""

from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
import check_cpp


def main() -> int:
    root = Path(sys.argv[1]).resolve()
    try:
        for source in check_cpp.discover_sources(root):
            print(source.relative_to(root).as_posix())
    except check_cpp.CheckError as error:
        print(error, file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
