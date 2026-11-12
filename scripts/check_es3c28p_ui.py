#!/usr/bin/env python3
"""Fail-closed, screenshot-free V1.1 UI fidelity gate.

Default: verify approved HTML against browser-computed layout.
--strict: additionally require formal LVGL page coverage and a runtime tree;
never claim 1:1 fidelity from only static source checks.
"""
import argparse
import hashlib
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ASSET = ROOT / "prototypes/esp32-ui"
BASELINE = ASSET / "ui-baseline.json"
HTML = ASSET / "index.html"
UI_CC = ROOT / "main/boards/lcdwiki-es3c28p/pixel_touch_ui.cc"
UI_H = ROOT / "main/boards/lcdwiki-es3c28p/pixel_touch_ui.h"

def issues(baseline, extracted):
    errors = []
    for key in ("source_sha256", "viewport", "icons", "actions", "keyframes", "states"):
        if extracted.get(key) != baseline.get(key):
            errors.append(f"{key} differs from approved baseline")
    return errors

def implementation_check(baseline, runtime_path):
    errors = []
    if not UI_CC.is_file() or not UI_H.is_file():
        return ["LVGL UI implementation files not found"]
    cc, header = UI_CC.read_text(encoding="utf-8"), UI_H.read_text(encoding="utf-8")
    for p in ("Home", "Voice", "Player", "Calendar", "Settings"):
        if f"Build{p}(" not in cc or f"Build{p}(" not in header:
            errors.append(f"missing formal LVGL page Build{p}")
    if runtime_path is None:
        errors.append("no LVGL runtime tree supplied (--runtime-tree FILE); geometry not verified")
        return errors
    if not runtime_path.exists():
        errors.append(f"runtime tree does not exist: {runtime_path}")
        return errors
    runtime = json.loads(runtime_path.read_text(encoding="utf-8"))
    if runtime.get("viewport") != baseline["viewport"]:
        errors.append("LVGL runtime viewport differs from 320x240 baseline")
    for state, spec in baseline["states"].items():
        actual = runtime.get("states", {}).get(state, {})
        if not actual:
            errors.append(f"missing runtime state {state}")
            continue
        observed = actual.get("anchors", {})
        repeats = {}
        for key, bounds, _style in spec["anchors"]:
            repeats[key] = repeats.get(key, 0) + 1
            runtime_key = key if repeats[key] == 1 else f"{key}#{repeats[key]}"
            a = observed.get(runtime_key)
            if a is None:
                errors.append(f"{state}: missing {runtime_key}")
                continue
            if len(a) != 4 or any(abs(float(a[i]) - bounds[i]) > 2 for i in range(4)):
                errors.append(f"{state}: geometry differs for {runtime_key}: {a} != {bounds}")
    return errors

def main():
    p = argparse.ArgumentParser()
    p.add_argument("--strict", action="store_true", help="require LVGL runtime geometry evidence")
    p.add_argument("--runtime-tree", type=Path)
    p.add_argument("--chromium", default=os.environ.get("CHROMIUM", ""))
    a = p.parse_args()
    errors = []
    baseline = json.loads(BASELINE.read_text(encoding="utf-8"))
    digest = hashlib.sha256(HTML.read_bytes()).hexdigest()
    if digest != baseline["source_sha256"]:
        errors.append("approved HTML changed: refuse silent baseline update")
    with tempfile.TemporaryDirectory() as temp:
        extracted_file = Path(temp) / "ui-baseline.json"
        cmd = [sys.executable, str(ROOT / "scripts/extract_es3c28p_ui.py"),
               "--output", str(extracted_file)]
        if a.chromium:
            cmd += ["--chromium", a.chromium]
        result = subprocess.run(cmd, capture_output=True, text=True)
        if result.returncode:
            errors.append("browser extraction failed: " + (result.stderr or result.stdout).strip()[-400:])
        else:
            errors.extend(issues(baseline, json.loads(extracted_file.read_text(encoding="utf-8"))))
    if a.strict:
        errors.extend(implementation_check(baseline, a.runtime_tree))
    if errors:
        for error in errors:
            print("FAIL:", error)
        return 1
    print("PASS: V1.1 HTML + 320x240 browser contract" +
          (" + LVGL geometry" if a.strict else " (runtime NOT verified)"))
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
