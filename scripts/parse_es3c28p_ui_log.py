#!/usr/bin/env python3
"""Convert ES3C28P LVGL diagnostic serial logs into real UI runtime bounds.

The output is observational evidence, not an approved visual baseline. Missing
states/anchors remain missing and the strict gate will fail rather than guess.
"""
import argparse
import json
import re
from pathlib import Path

LINE = re.compile(
    r"ES3C28P_UI:\s+state=(\S+)\s+anchor=(\S+)"
    r"\s+x=(-?\d+)\s+y=(-?\d+)\s+w=(\d+)\s+h=(\d+)"
)
BEGIN = re.compile(r"ES3C28P_UI:\s+state=(\S+)\s+begin=1")
VIEWPORT = {"width": 320, "height": 240, "orientation": "landscape", "dpr": 1}

def parse(text):
    states = {}
    begun = set()
    count = 0
    for line in text.splitlines():
        start = BEGIN.search(line)
        if start:
            state = start.group(1)
            # Only the last complete capture is authoritative. Never combine
            # geometry from different times or prior boots into a fake pass.
            states[state] = {"anchors": {}}
            begun.add(state)
            continue
        match = LINE.search(line)
        if not match:
            continue
        state, anchor = match.group(1), match.group(2)
        states.setdefault(state, {"anchors": {}})["anchors"][anchor] = [
            int(match.group(i)) for i in range(3, 7)
        ]
        count += 1
    format_version = 2 if states and set(states).issubset(begun) else 1
    return {"viewport": VIEWPORT, "capture_format": format_version,
            "states": states}, count

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("serial_log", type=Path)
    ap.add_argument("--output", type=Path, required=True)
    args = ap.parse_args()
    result, count = parse(args.serial_log.read_text(encoding="utf-8", errors="replace"))
    if count == 0:
        ap.error("no ES3C28P_UI entries detected; use diagnostics firmware")
    if result["capture_format"] != 2:
        ap.error("outdated or truncated geometry log: missing snapshot begin markers")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n",
                           encoding="utf-8")
    print(f"{count} LVGL measurements, {len(result['states'])} observed states -> {args.output}")

if __name__ == "__main__":
    main()
