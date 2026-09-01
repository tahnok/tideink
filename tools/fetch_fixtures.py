#!/usr/bin/env python3
"""Capture live IWLS responses as simulator fixtures.

The firmware and the simulator run the same parser, so a fixture recorded here
is exactly what the device will see on the wire:

    python3 tools/fetch_fixtures.py                      # refresh from "now"
    python3 tools/fetch_fixtures.py --from 2026-08-08    # reproducible capture

Station IDs come from https://api-iwls.dfo-mpo.gc.ca/api/v1/stations
"""

from __future__ import annotations

import argparse
import datetime as dt
import json
import os
import urllib.request

API = "https://api-iwls.dfo-mpo.gc.ca/api/v1"
# Ile d'Entree, QC -- CHS station 01966.
DEFAULT_STATION = "5cebf1e13d0f4a073c4bbf06"
FIXTURE_DIR = os.path.normpath(os.path.join(os.path.dirname(__file__), "..", "test", "fixtures"))


def iso(t: dt.datetime) -> str:
    return t.strftime("%Y-%m-%dT%H:%M:%SZ")


def fetch(station: str, series: str, start: dt.datetime, end: dt.datetime, resolution=None) -> str:
    url = f"{API}/stations/{station}/data?time-series-code={series}&from={iso(start)}&to={iso(end)}"
    if resolution:
        url += f"&resolution={resolution}"
    with urllib.request.urlopen(url, timeout=60) as r:
        body = r.read().decode("utf-8")
    json.loads(body)  # fail loudly on an error page
    return body


def main() -> None:
    p = argparse.ArgumentParser()
    p.add_argument("--station", default=DEFAULT_STATION)
    p.add_argument("--name", default="ile_dentree")
    p.add_argument(
        "--from",
        dest="start",
        default=None,
        help="UTC start date/time (YYYY-MM-DD or ISO8601); defaults to the current hour",
    )
    p.add_argument("--hours", type=int, default=48, help="length of the water level series")
    p.add_argument("--resolution", default="SIXTY_MINUTES")
    args = p.parse_args()

    if args.start:
        s = args.start if "T" in args.start else args.start + "T00:00:00"
        start = dt.datetime.fromisoformat(s).replace(tzinfo=None)
    else:
        start = dt.datetime.utcnow().replace(minute=0, second=0, microsecond=0)
    end = start + dt.timedelta(hours=args.hours)

    os.makedirs(FIXTURE_DIR, exist_ok=True)

    curve = fetch(args.station, "wlp", start, end, args.resolution)
    # A little slack on both sides so there is always a "next" high and low.
    hilo = fetch(
        args.station, "wlp-hilo", start - dt.timedelta(hours=12), end + dt.timedelta(hours=12)
    )

    for series, body in (("wlp", curve), ("hilo", hilo)):
        path = os.path.join(FIXTURE_DIR, f"{args.name}_{series}.json")
        with open(path, "w") as f:
            f.write(body)
        print(f"{path}: {len(json.loads(body))} points, {len(body)} bytes")

    print(f"window: {iso(start)} .. {iso(end)}")


if __name__ == "__main__":
    main()
