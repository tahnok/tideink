#!/usr/bin/env python3
"""Render every UI state to docs/screenshots/ in one go.

    pio run -e sim -t screenshots

Each entry below is a state worth eyeballing after a layout change: the normal
screen, the clock-format variant, degraded states, and the first-boot screen.
"""

from __future__ import annotations

import argparse
import os
import subprocess
import sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(__file__), ".."))
OUT_DIR = os.path.join(ROOT, "docs", "screenshots")

STATION = "CHARLOTTETOWN, PE"
NOTE = "CHS station 01700  46.23N 63.12W"

# name -> extra arguments
SCENES = {
    "01-falling": ["--now", "2026-08-08T12:00:00Z"],
    "02-rising": ["--now", "2026-08-08T19:30:00Z"],
    "03-near-high": ["--now", "2026-08-08T22:30:00Z"],
    "04-24h-clock": ["--now", "2026-08-08T12:00:00Z", "--24h"],
    "05-charging": ["--now", "2026-08-08T12:00:00Z", "--battery", "34", "--charging"],
    "06-stale-cache": [
        "--now",
        "2026-08-09T05:00:00Z",
        "--battery",
        "9",
        "--fetched-ago",
        "97200",
        "--banner",
        "WI-FI UNREACHABLE - SHOWING CACHED PREDICTIONS",
    ],
    "07-first-boot": [
        "--message",
        "Waiting for Wi-Fi|Connecting to the network and downloading|tide predictions from the CHS.",
    ],
}


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--binary", default=os.path.join(ROOT, ".pio", "build", "sim", "program"))
    args = p.parse_args()

    if not os.path.isfile(args.binary):
        sys.exit(f"{args.binary} not found -- run `pio run -e sim` first")
    os.makedirs(OUT_DIR, exist_ok=True)

    for name, extra in SCENES.items():
        out = os.path.join(OUT_DIR, f"{name}.png")
        cmd = [
            args.binary,
            "--hilo", os.path.join(ROOT, "test", "fixtures", "charlottetown_hilo.json"),
            "--wlp", os.path.join(ROOT, "test", "fixtures", "charlottetown_wlp.json"),
            "--station", STATION,
            "--note", NOTE,
            "--out", out,
        ] + extra
        result = subprocess.run(cmd, cwd=ROOT)
        if result.returncode != 0:
            return result.returncode
        print(f"wrote {os.path.relpath(out, ROOT)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
