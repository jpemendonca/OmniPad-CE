#!/usr/bin/env python3
"""Probe an XInput (Xbox 360-style) USB pad from Linux/WSL, mimicking what
OmniPad does on the PS5, to find out which init step breaks or fixes reporting.

XInput pads only report on change, so keep rotating a stick during the run.
Every step first waits for the pad to show activity, then sends one command,
then counts how many reports arrive afterwards:
  baseline  no command at all
  vendor    control request 0xC1/0x01/0x0100 (len 20), the xpad "third-party init"
  led_out   Xbox 360 LED command on the OUT endpoint (01 03 02)
  set_rep   the same LED pattern as a HID SET_REPORT control transfer (OmniPad's handshake 2)

Once the 8BitDo Ultimate 2C gets led_out it keeps reporting until the controller is
switched off, so power-cycle it (hold Home ~3s) before a run that must start "locked".

Usage: sudo python3 probe_xinput.py [vid:pid] [seconds_per_step] [step,step,...]
"""
import sys
import time

import usb.core
import usb.util

vid, pid = (int(x, 16) for x in (sys.argv[1] if len(sys.argv) > 1 else "2dc8:3106").split(":"))
secs = float(sys.argv[2]) if len(sys.argv) > 2 else 8

dev = usb.core.find(idVendor=vid, idProduct=pid)
if dev is None:
    sys.exit(f"device {vid:04x}:{pid:04x} not found")
if dev.is_kernel_driver_active(0):
    dev.detach_kernel_driver(0)
dev.set_configuration()
intf = dev.get_active_configuration()[(0, 0)]
ep_in = usb.util.find_descriptor(intf, custom_match=lambda e: usb.util.endpoint_direction(e.bEndpointAddress) == usb.util.ENDPOINT_IN)
ep_out = usb.util.find_descriptor(intf, custom_match=lambda e: usb.util.endpoint_direction(e.bEndpointAddress) == usb.util.ENDPOINT_OUT)
print(f"IN=0x{ep_in.bEndpointAddress:02x} OUT=0x{ep_out.bEndpointAddress:02x}", flush=True)


def read_one(timeout_ms):
    try:
        return bytes(ep_in.read(64, timeout=timeout_ms))
    except usb.core.USBTimeoutError:
        return None
    except usb.core.USBError as e:
        print(f"  read error: {e}", flush=True)
        time.sleep(0.05)
        return None


def wait_for_activity(limit_s=120):
    print("  waiting for stick/button activity...", flush=True)
    end = time.time() + limit_s
    seen = 0
    while time.time() < end and seen < 5:
        if read_one(200):
            seen += 1
    if seen < 5:
        sys.exit("  no activity seen, giving up")


def count_reports(name):
    end = time.time() + secs
    count, first = 0, None
    while time.time() < end:
        data = read_one(50)
        if data:
            count += 1
            first = first or data
    sample = first.hex(" ") if first else "-"
    print(f"step {name}: {count} reports in {secs:.0f}s (first: {sample})", flush=True)


def step(name, action):
    wait_for_activity()
    if action:
        try:
            print(f"  {name}: {action()}", flush=True)
        except usb.core.USBError as e:
            print(f"  {name}: FAILED ({e})", flush=True)
    count_reports(name)


STEPS = {
    "baseline": None,
    "vendor": lambda: f"{len(dev.ctrl_transfer(0xC1, 0x01, 0x0100, 0x0000, 20, timeout=200))} bytes back",
    "led_out": lambda: f"{ep_out.write(bytes([0x01, 0x03, 0x02]), timeout=200)} bytes sent",
    "set_rep": lambda: f"{dev.ctrl_transfer(0x21, 0x09, 0x0200, 0x0000, bytes([0x01, 0x03, 0x02]), timeout=200)} bytes accepted",
}

# optional 3rd arg: comma-separated step order, e.g. "baseline,set_rep"
order = sys.argv[3].split(",") if len(sys.argv) > 3 else list(STEPS)
for name in order:
    step(name, STEPS[name])

usb.util.dispose_resources(dev)
