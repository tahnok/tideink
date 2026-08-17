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

# name -> extra arguments
SCENES = {
    # Mid-morning on a day whose first high fell before the 6 am boundary, so
    # the row starts on a low and only three tides belong to the day.
    "01-morning": ["--now", "2026-08-08T12:00:00Z"],
    # A four-tide day, seen in the evening with two of them already past.
    "02-evening": ["--now", "2026-08-09T20:00:00Z", "--battery", "54"],
    # After midnight but before 6 am, so the window is still the one that opened
    # yesterday morning and the "now" cursor sits near its right-hand edge.
    "03-small-hours": ["--now", "2026-08-10T04:30:00Z", "--battery", "31"],
    "04-24h-clock": ["--now", "2026-08-09T20:00:00Z", "--24h"],
    "05-stale-cache": [
        "--now",
        "2026-08-09T05:00:00Z",
        "--fetched-ago",
        "97200",
        "--battery",
        "9",
        "--banner",
        "WI-FI UNREACHABLE - SHOWING CACHED PREDICTIONS",
    ],
    "06-first-boot": [
        "--message",
        "Waiting for Wi-Fi|Connecting to the network and downloading|tide predictions from the CHS.",
    ],
    # On the cable, topping up.
    "07-charging": ["--now", "2026-08-09T13:00:00Z", "--battery", "83", "--charging"],
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
            "--out", out,
        ] + extra
        result = subprocess.run(cmd, cwd=ROOT)
        if result.returncode != 0:
            return result.returncode
        print(f"wrote {os.path.relpath(out, ROOT)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
