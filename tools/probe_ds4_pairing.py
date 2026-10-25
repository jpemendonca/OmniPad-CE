#!/usr/bin/env python3
"""Read which Bluetooth host a DualShock 4 is paired with, over its USB cable (Linux/WSL + usbipd).

The DS4 answers feature report 0x12 with its own address and the address of the host it calls when
PS is pressed. If that host is not OmniPad's adapter, the pad will never call the adapter.

Usage: sudo python3 probe_ds4_pairing.py [vid:pid]   (default 054c:05c4, also 054c:09cc)
"""
import sys

import usb.core
import usb.util


def addr(b):
    return ":".join(f"{x:02X}" for x in reversed(b))


ids = [sys.argv[1]] if len(sys.argv) > 1 else ["054c:05c4", "054c:09cc"]
dev = None
for s in ids:
    vid, pid = (int(x, 16) for x in s.split(":"))
    dev = usb.core.find(idVendor=vid, idProduct=pid)
    if dev:
        break
if dev is None:
    sys.exit("no DualShock 4 found on USB")
try:
    if dev.is_kernel_driver_active(0):
        dev.detach_kernel_driver(0)
except usb.core.USBError:
    pass

# HID GET_REPORT, feature report 0x12, interface 0
r = bytes(dev.ctrl_transfer(0xA1, 0x01, 0x0312, 0, 16, timeout=1000))
print(f"feature 0x12: {r.hex(' ')}")
if len(r) >= 16 and r[0] == 0x12:
    print(f"pad address:  {addr(r[1:7])}")
    print(f"paired host:  {addr(r[10:16])}")
usb.util.dispose_resources(dev)
