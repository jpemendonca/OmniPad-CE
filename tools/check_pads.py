#!/usr/bin/env python3
"""Measures every connected controller on the PS5 and compares it with the table in SPECS.md.

Move the sticks of every controller while it runs: most pads only report on a change.
Exits with 1 when a controller fell under 85% of its reference or stopped reporting.

Usage: python tools/check_pads.py <PS5_IP> [seconds]     (default 20 s)
"""
import json
import re
import sys
import time
import urllib.error
import urllib.request
from pathlib import Path

SPECS = Path(__file__).resolve().parent.parent / "SPECS.md"
MIN_SHARE = 0.85
LINKS = {1: "Bluetooth", 2: "Bluetooth", 3: "USB", 4: "2.4 GHz dongle"}
GAP_WARN_MS = 500   # a DS4 with gaps over 1 s was the bug of 2 Nov


def read_reference():
    """(controller, link, adapter, reports/s, injections/s) for each row of the table."""
    rows = []
    for line in SPECS.read_text(encoding="utf-8").splitlines():
        cells = [c.strip() for c in line.strip().strip("|").split("|")]
        if len(cells) == 6 and re.fullmatch(r"\d+", cells[3]) and re.fullmatch(r"\d+", cells[4]):
            rows.append((cells[0], cells[1], cells[2], int(cells[3]), int(cells[4])))
    return rows


def fetch(base):
    with urllib.request.urlopen(base + "/api/state", timeout=3) as res:
        return json.loads(res.read().decode("utf-8", "replace"))


def reference_for(rows, name, link, adapter):
    """The row of this controller on this link, the one with the same adapter first."""
    same = [r for r in rows if r[0].lower() in name.lower() and r[1].lower() in link.lower()]
    for row in same:
        if row[1] != "Bluetooth" or adapter_matches(row[2], adapter):
            return row, True
    return (same[0], False) if same else (None, False)


def adapter_matches(reference, adapter):
    words = re.findall(r"[a-z0-9]{4,}", reference.lower())
    return any(w in adapter.lower() for w in words)


def measure(base, seconds):
    first = fetch(base)
    start = time.monotonic()
    gaps = [0] * len(first["slots"])
    last = first
    while time.monotonic() - start < seconds:
        time.sleep(0.2)
        last = fetch(base)
        for i, slot in enumerate(last["slots"]):
            gaps[i] = max(gaps[i], slot.get("input_age_ms", -1))
    return first, last, time.monotonic() - start, gaps


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    base = "http://" + sys.argv[1].removeprefix("http://").rstrip("/")
    if ":" not in base[7:]:
        base += ":8095"
    seconds = float(sys.argv[2]) if len(sys.argv) > 2 else 20
    rows = read_reference()

    try:
        print(f"Measuring for {seconds:.0f} s. Move the sticks of every controller.")
        first, last, elapsed, gaps = measure(base, seconds)
    except (urllib.error.URLError, TimeoutError, OSError) as e:
        print(f"{base} does not answer ({e}). If the PS5 answers ping, its IP may have changed.")
        return 2

    adapter = (last.get("bt") or {}).get("adapter", "")
    print(f"OmniPad {last.get('version')} build {last.get('build')}; adapter: {adapter or 'none'}")

    failed = connected = 0
    for i, (a, b) in enumerate(zip(first["slots"], last["slots"])):
        if not b.get("status"):
            continue
        connected += 1
        reports = (b["inputs"] - a["inputs"]) / elapsed
        injections = (b["injected"] - a["injected"]) / elapsed
        link = LINKS.get(b.get("conn"), "?")
        line = (f"player {i + 2}: {b.get('name') or '?'} {b.get('vid', 0):04x}:{b.get('pid', 0):04x} over {link}: "
                f"{reports:.0f} reports/s, {injections:.0f} injections/s, longest silence {gaps[i]} ms")
        row, same_adapter = reference_for(rows, b.get("name") or "", link, adapter)
        if reports < 1:
            verdict = "NOT REPORTING: was it moved?"
            failed += 1
        elif not row:
            verdict = "no reference yet: add a row to the table in SPECS.md"
        elif not same_adapter:
            verdict = f"reference is for {row[2]} ({row[3]} reports/s), not this adapter"
        elif reports < row[3] * MIN_SHARE or injections < row[4] * MIN_SHARE:
            verdict = f"DROPPED: reference {row[3]} reports/s, {row[4]} injections/s ({row[2]})"
            failed += 1
        else:
            verdict = f"ok, reference {row[3]} reports/s"
        print(f"  {line}\n    {verdict}")
        if reports >= 1 and gaps[i] > GAP_WARN_MS:
            print(f"    silences over {GAP_WARN_MS} ms: fine if the stick was still, a problem if it was moving")

    if not connected:
        print("No controller connected: nothing was measured.")
        return 2
    print("Something fell behind its reference." if failed else "Nothing fell behind its reference.")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
