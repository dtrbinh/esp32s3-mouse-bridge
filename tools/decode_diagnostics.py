#!/usr/bin/env python3
"""Decode rate-limited MB_DIAG lines from one or both firmware monitors."""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path
from typing import Iterable

PAIR = re.compile(r"([A-Za-z_][A-Za-z0-9_]*)=([^\s]+)")
INTEGER = re.compile(r"^-?[0-9]+$")


def decode_line(line: str) -> dict[str, object] | None:
    if "MB_DIAG" not in line:
        return None
    fields: dict[str, object] = {}
    for key, raw in PAIR.findall(line):
        fields[key] = int(raw) if INTEGER.match(raw) else raw
    if "board" not in fields:
        return None
    if isinstance(fields.get("ble_interval_units"), int):
        fields["ble_interval_ms"] = fields["ble_interval_units"] * 1.25
    return fields


def input_lines(paths: list[Path]) -> Iterable[str]:
    if not paths:
        yield from sys.stdin
        return
    for path in paths:
        with path.open(encoding="utf-8", errors="replace") as stream:
            yield from stream


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("paths", nargs="*", type=Path, help="monitor log files")
    parser.add_argument(
        "--latest", action="store_true", help="emit only the last sample per board"
    )
    args = parser.parse_args()

    latest: dict[str, dict[str, object]] = {}
    found = False
    for line in input_lines(args.paths):
        decoded = decode_line(line)
        if decoded is None:
            continue
        found = True
        board = str(decoded["board"])
        latest[board] = decoded
        if not args.latest:
            print(json.dumps(decoded, sort_keys=True), flush=True)

    if args.latest:
        for board in sorted(latest):
            print(json.dumps(latest[board], sort_keys=True))
    return 0 if found else 1


if __name__ == "__main__":
    raise SystemExit(main())
