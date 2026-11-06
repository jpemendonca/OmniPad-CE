#!/usr/bin/env python3
"""Records one controller on the PS5 while you follow a short script, and saves it to tests/captures/.

`make test` then replays every recording through the parsers of each new build, so a change that
alters how an approved controller decodes fails on the PC before anyone tests it on the PS5.

Close the dashboard while recording: it reads the same "pressed since last look" flags.
Each step waits until OmniPad sees it. Press Enter to skip a button your controller lacks.

Usage: python tools/record_pads.py <PS5_IP> [--slot N] [--idle SECONDS]
"""
import argparse
import json
import sys
import time
import urllib.error
import urllib.request
from datetime import date
from pathlib import Path

OUT_DIR = Path(__file__).resolve().parent.parent / "tests" / "captures"
LINKS = {1: "bt", 2: "bt", 3: "usb", 4: "dongle"}
STEP_TIMEOUT = 20

# Bits of pad_state_t.buttons (src/pad_types.h)
BUTTONS = [
    ("cross", "Cross / A", 1 << 14), ("circle", "Circle / B", 1 << 13),
    ("square", "Square / X", 1 << 15), ("triangle", "Triangle / Y", 1 << 12),
    ("up", "D-pad up", 1 << 4), ("down", "D-pad down", 1 << 6),
    ("left", "D-pad left", 1 << 7), ("right", "D-pad right", 1 << 5),
    ("l1", "L1 / LB", 1 << 10), ("r1", "R1 / RB", 1 << 11),
    ("l2", "L2 / LT", 1 << 8), ("r2", "R2 / RT", 1 << 9),
    ("l3", "L3 (press the left stick)", 1 << 1), ("r3", "R3 (press the right stick)", 1 << 2),
    ("create", "Create / Share / View / Select", 1 << 0), ("options", "Options / Menu / Start", 1 << 3),
    ("touchpad", "Touchpad click", 1 << 20),
    ("ps", "PS / Home / Guide (the console menu may open: that is fine)", 1 << 16),
]


class Ps5:
    def __init__(self, host):
        host = host.removeprefix("http://").rstrip("/")
        self.base = "http://" + (host if ":" in host else host + ":8095")

    def _open(self, path, method="GET"):
        req = urllib.request.Request(self.base + path, method=method, data=b"" if method == "POST" else None)
        return urllib.request.urlopen(req, timeout=5)

    def state(self):
        with self._open("/api/state") as res:
            return json.loads(res.read().decode("utf-8", "replace"))

    def post(self, path):
        with self._open(path, "POST") as res:
            return json.loads(res.read().decode("utf-8", "replace"))

    def download(self):
        lines, since = [], 0
        while True:
            with self._open(f"/api/capture?since={since}") as res:
                body = res.read().decode("utf-8", "replace")
                nxt = int(res.headers["X-Capture-Next"])
                total = int(res.headers["X-Capture-Total"])
                state = int(res.headers["X-Capture-State"])
            lines.extend(l for l in body.splitlines() if l)
            if nxt >= total and state != 1:
                return lines, state
            if nxt == since:
                time.sleep(0.2)
            since = nxt


def enter_pressed():
    """True if Enter was pressed, without waiting."""
    try:
        import msvcrt
        hit = False
        while msvcrt.kbhit():
            hit = msvcrt.getwch() in "\r\n" or hit
        return hit
    except ImportError:
        import select
        if select.select([sys.stdin], [], [], 0)[0]:
            sys.stdin.readline()
            return True
        return False


def wait_for(ps5, slot, test, prompt, mark):
    """Polls the slot until test(slot_state) is true. Returns False if skipped or timed out."""
    ps5.post(f"/api/capture_mark?label={mark}")
    print("  " + prompt, end="", flush=True)
    start = time.monotonic()
    while time.monotonic() - start < STEP_TIMEOUT:
        if enter_pressed():
            print("  skipped")
            ps5.post(f"/api/capture_mark?label=skipped_{mark}")
            return False
        if test(ps5.state()["slots"][slot]):
            print("  ok")
            return True
        time.sleep(0.1)
    print("  not seen, skipped")
    ps5.post(f"/api/capture_mark?label=timeout_{mark}")
    return False


def wait_released(ps5, slot):
    quiet_since = None
    while True:
        s = ps5.state()["slots"][slot]
        if s["raw_buttons"] == 0 and s["l2"] < 20 and s["r2"] < 20:
            quiet_since = quiet_since or time.monotonic()
            if time.monotonic() - quiet_since > 0.3:
                return
        else:
            quiet_since = None
        time.sleep(0.05)


def stick_circles(axis_x, axis_y):
    seen = {"min_x": 255, "max_x": 0, "min_y": 255, "max_y": 0}

    def test(s):
        seen["min_x"] = min(seen["min_x"], s[axis_x]); seen["max_x"] = max(seen["max_x"], s[axis_x])
        seen["min_y"] = min(seen["min_y"], s[axis_y]); seen["max_y"] = max(seen["max_y"], s[axis_y])
        return seen["min_x"] < 40 and seen["max_x"] > 215 and seen["min_y"] < 40 and seen["max_y"] > 215
    return test


def trigger_sweep(axis):
    seen = {"top": False}

    def test(s):
        seen["top"] = seen["top"] or s[axis] > 240
        return seen["top"] and s[axis] < 10
    return test


def pick_slot(ps5, wanted):
    slots = ps5.state()["slots"]
    live = [i for i, s in enumerate(slots) if s.get("status")]
    if wanted is not None:
        if wanted not in live:
            sys.exit(f"Slot {wanted} has no controller. Connected: {live or 'none'}")
        return wanted
    if not live:
        sys.exit("No controller connected. Connect one and run this again.")
    if len(live) == 1:
        return live[0]
    for i in live:
        print(f"  slot {i}: {slots[i]['name']} ({slots[i]['vid']:04x}:{slots[i]['pid']:04x})")
    return int(input("Which slot? "))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("host")
    ap.add_argument("--slot", type=int)
    ap.add_argument("--idle", type=int, default=25, help="seconds untouched at the end (default 25)")
    args = ap.parse_args()
    ps5 = Ps5(args.host)

    try:
        slot = pick_slot(ps5, args.slot)
        info = ps5.state()
        pad = info["slots"][slot]
        print(f"Recording {pad['name']} ({pad['vid']:04x}:{pad['pid']:04x}) on slot {slot}, "
              f"OmniPad {info['version']} build {info['build']}. Close the dashboard first.")
        if ps5.post(f"/api/capture_start?slot={slot}&seconds=300").get("status") != "recording":
            sys.exit("OmniPad could not start recording (no memory?).")
        ps5.state()  # clears the "pressed since" flags left from before

        print("Buttons, one at a time:")
        for key, label, bit in BUTTONS:
            wait_for(ps5, slot, lambda s, b=bit: (s["raw_seen"] | s["raw_buttons"]) & b, f"Press {label}.", f"press_{key}")
            wait_released(ps5, slot)

        print("Sticks and triggers:")
        wait_for(ps5, slot, stick_circles("lx", "ly"), "Left stick: slow full circles.", "left_stick")
        wait_for(ps5, slot, stick_circles("rx", "ry"), "Right stick: slow full circles.", "right_stick")
        wait_for(ps5, slot, trigger_sweep("l2"), "L2 slowly all the way down, then let go.", "l2_sweep")
        wait_for(ps5, slot, trigger_sweep("r2"), "R2 slowly all the way down, then let go.", "r2_sweep")

        print(f"Leave the controller untouched for {args.idle} s.", flush=True)
        ps5.post("/api/capture_mark?label=idle")
        time.sleep(args.idle)
        ps5.post("/api/capture_stop")
        lines, state = ps5.download()
    except (urllib.error.URLError, TimeoutError, OSError) as e:
        sys.exit(f"{ps5.base} does not answer ({e}). If the PS5 answers ping, its IP may have changed.")

    reports = sum(1 for l in lines if l[0] in "UB")
    if state == 3:
        print("The buffer filled before the end: the recording is shorter than the script.")
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    name = "".join(c if c.isalnum() else "_" for c in pad["name"].lower()).strip("_")
    path = OUT_DIR / f"{pad['vid']:04x}_{pad['pid']:04x}_{LINKS.get(pad['conn'], 'x')}_{name}.txt"
    header = [f"# {pad['name']} {pad['vid']:04x}:{pad['pid']:04x} over {LINKS.get(pad['conn'], '?')}",
              f"# recorded {date.today()} on OmniPad {info['version']} build {info['build']}, {reports} reports"]
    path.write_text("\n".join(header + lines) + "\n", encoding="utf-8")
    print(f"Saved {reports} reports to {path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
