#!/usr/bin/env python3
"""Summarize ES3C28P diagnostic logs without ESP-IDF dependencies."""

import argparse
import re
from collections import defaultdict
from pathlib import Path

FIELDS = (
    "internal_free",
    "internal_min",
    "internal_largest",
    "psram_free",
    "psram_min",
    "psram_largest",
)
SAMPLE_RE = re.compile(
    r"ES3C28P_RES:\s+uptime_ms=(?P<uptime_ms>\d+)\s+"
    r"state=(?P<state>[a-z_]+)\s+"
    + r"\s+".join(fr"{name}=(?P<{name}>\d+)" for name in FIELDS)
)


def parse_samples(lines):
    for line in lines:
        match = SAMPLE_RE.search(line)
        if match is None:
            continue
        item = match.groupdict()
        for key in ("uptime_ms",) + FIELDS:
            item[key] = int(item[key])
        yield item


def summarize(samples):
    groups = defaultdict(list)
    for sample in samples:
        groups[sample["state"]].append(sample)
    return {
        state: {
            "count": len(values),
            "min_internal_free": min(x["internal_free"] for x in values),
            "min_internal_largest": min(x["internal_largest"] for x in values),
            "min_psram_free": min(x["psram_free"] for x in values),
            "min_psram_largest": min(x["psram_largest"] for x in values),
        }
        for state, values in sorted(groups.items())
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path, help="Saved serial monitor log")
    args = parser.parse_args()
    with args.log.open(encoding="utf-8", errors="replace") as src:
        samples = list(parse_samples(src))
    if not samples:
        parser.error("No ES3C28P_RES samples found; flash the -diag variant first")

    print(f"Samples: {len(samples)}; duration: {samples[-1]['uptime_ms'] - samples[0]['uptime_ms']} ms")
    print("State | samples | min internal free | min internal largest | min PSRAM free | min PSRAM largest (bytes)")
    print("--- | ---: | ---: | ---: | ---: | ---:")
    for state, summary in summarize(samples).items():
        print(f"{state} | {summary['count']} | {summary['min_internal_free']} | "
              f"{summary['min_internal_largest']} | {summary['min_psram_free']} | "
              f"{summary['min_psram_largest']}")
    print("Global boot-time low watermarks: "
          f"internal={min(s['internal_min'] for s in samples)} "
          f"psram={min(s['psram_min'] for s in samples)} bytes")
    print("Note: state-specific free/largest are samples; *_min counters are boot-wide low watermarks.")


if __name__ == "__main__":
    main()
