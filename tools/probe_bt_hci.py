#!/usr/bin/env python3
"""Talk raw HCI to a USB Bluetooth adapter from Linux/WSL (usbipd + pyusb), to check whether
a given adapter can be driven by OmniPad before writing payload code for it.

Commands:
  info                 HCI reset, chip version, manufacturer, address (and Realtek ROM version)
  inquiry [seconds]    list nearby devices (put a DS4 in pairing mode: hold Share + PS)
  connect <addr>       open a baseband link to <addr> and close it again
  rtlfw <fw> [config]  load a Realtek firmware patch (linux-firmware rtl_bt/*_fw.bin + *_config.bin);
                       stays until the adapter loses power

Usage: sudo python3 probe_bt_hci.py [vid:pid] <command> [args]   (default 2357:0604)
"""
import struct
import sys
import time

import usb.core
import usb.util

MANUFACTURERS = {0x000A: "CSR / Qualcomm", 0x000F: "Broadcom", 0x0002: "Intel", 0x005D: "Realtek",
                 0x0046: "MediaTek", 0x0048: "Marvell", 0x001D: "Qualcomm"}

args = sys.argv[1:]
vidpid = "2357:0604"
if args and ":" in args[0] and len(args[0]) == 9:
    vidpid = args.pop(0)
if not args:
    sys.exit(__doc__)
cmd = args.pop(0)
vid, pid = (int(x, 16) for x in vidpid.split(":"))

dev = usb.core.find(idVendor=vid, idProduct=pid)
if dev is None:
    sys.exit(f"adapter {vidpid} not found")
for itf in (0, 1):
    try:
        if dev.is_kernel_driver_active(itf):
            dev.detach_kernel_driver(itf)
    except usb.core.USBError:
        pass
cfg = dev.get_active_configuration()
intf = cfg[(0, 0)]
usb.util.claim_interface(dev, 0)
ep_evt = usb.util.find_descriptor(intf, custom_match=lambda e: usb.util.endpoint_type(e.bmAttributes) == usb.util.ENDPOINT_TYPE_INTR
                                  and usb.util.endpoint_direction(e.bEndpointAddress) == usb.util.ENDPOINT_IN)
print(f"adapter {vidpid}: events on 0x{ep_evt.bEndpointAddress:02x}", flush=True)

pending = b""


def read_event(timeout_ms=1000):
    """One complete HCI event (code, params), reassembled from interrupt packets."""
    global pending
    deadline = time.time() + timeout_ms / 1000
    while True:
        if len(pending) >= 2 and len(pending) >= 2 + pending[1]:
            code, plen = pending[0], pending[1]
            params, pending = pending[2:2 + plen], pending[2 + plen:]
            return code, params
        left = int((deadline - time.time()) * 1000)
        if left <= 0:
            return None
        try:
            pending += bytes(ep_evt.read(64, timeout=max(left, 1)))
        except usb.core.USBTimeoutError:
            return None


def send(opcode, params=b""):
    pkt = struct.pack("<HB", opcode, len(params)) + params
    dev.ctrl_transfer(0x20, 0x00, 0, 0, pkt, timeout=1000)


def command(opcode, params=b"", timeout_ms=2000):
    """Send a command and return the Command Complete parameters after the status byte check."""
    send(opcode, params)
    deadline = time.time() + timeout_ms / 1000
    while time.time() < deadline:
        ev = read_event(int((deadline - time.time()) * 1000) + 1)
        if not ev:
            break
        code, p = ev
        if code == 0x0E and len(p) >= 3 and struct.unpack_from("<H", p, 1)[0] == opcode:
            return p[3:]
        if code == 0x0F and len(p) >= 4 and struct.unpack_from("<H", p, 2)[0] == opcode:
            return bytes([p[0]])
    raise TimeoutError(f"no reply to command 0x{opcode:04x}")


def addr_str(b):
    return ":".join(f"{x:02X}" for x in reversed(b))


def addr_bytes(s):
    return bytes(reversed([int(x, 16) for x in s.split(":")]))


def eir_name(eir):
    i = 0
    while i + 1 < len(eir) and eir[i]:
        ln, typ = eir[i], eir[i + 1]
        if typ in (0x08, 0x09):
            return eir[i + 2:i + 1 + ln].decode("utf-8", "replace")
        i += ln + 1
    return ""


def do_info():
    st = command(0x0C03)  # HCI_Reset
    print(f"reset: status {st[0]:#04x}")
    v = command(0x1001)  # Read_Local_Version_Information
    status, hci_ver, hci_rev, lmp_ver, manuf, lmp_sub = struct.unpack_from("<BBHBHH", v)
    print(f"version: status {status:#04x} HCI {hci_ver} rev {hci_rev:#06x} LMP {lmp_ver} subversion {lmp_sub:#06x}")
    print(f"manufacturer: {manuf:#06x} {MANUFACTURERS.get(manuf, '?')}")
    a = command(0x1009)  # Read_BD_ADDR
    print(f"address: {addr_str(a[1:7])}")
    if manuf == 0x005D:
        try:
            r = command(0xFC6D)  # Realtek: read ROM version
            print(f"realtek rom version: status {r[0]:#04x} value {r[1:].hex(' ')}")
        except TimeoutError as e:
            print(f"realtek rom version: {e}")
        print("realtek firmware: lmp subversion above is the ROM build when no patch has been loaded")
    n = command(0x0C14)  # Read_Local_Name
    name = n[1:].split(b"\0")[0].decode("utf-8", "replace")
    print(f"local name: {name!r}")


def do_inquiry(seconds):
    command(0x0C03)
    command(0x0C45, b"\x02")  # Write_Inquiry_Mode: extended results
    length = max(1, min(0x30, int(round(seconds / 1.28))))
    st = command(0x0401, bytes([0x33, 0x8B, 0x9E, length, 0]))  # Inquiry, GIAC
    print(f"inquiry: started ({length * 1.28:.0f}s), status {st[0]:#04x}", flush=True)
    seen = set()
    t0 = time.time()
    deadline = t0 + length * 1.28 + 3
    while time.time() < deadline:
        ev = read_event(500)
        if not ev:
            continue
        code, p = ev
        if code == 0x01:
            print(f"inquiry: complete after {time.time() - t0:.1f}s, status {p[0]:#04x}")
            break
        if code not in (0x02, 0x22, 0x2F):
            print(f"  event {code:#04x}: {p.hex(' ')}", flush=True)
        if code in (0x02, 0x22, 0x2F) and len(p) >= 15:
            addr = addr_str(p[1:7])
            if code == 0x2F:
                cod = p[9] | p[10] << 8 | p[11] << 16
                rssi = struct.unpack_from("<b", p, 14)[0]
                name = eir_name(p[15:])
            else:
                off = 9 if code == 0x22 else 10
                cod = p[off] | p[off + 1] << 8 | p[off + 2] << 16
                rssi, name = None, ""
            if addr not in seen:
                seen.add(addr)
                gamepad = (cod >> 8) & 0x1F == 0x05 and (cod >> 2) & 0x0F == 0x02
                print(f"  {addr} class {cod:06x}{' (gamepad)' if gamepad else ''} rssi {rssi} name {name!r}", flush=True)
    print(f"inquiry: {len(seen)} device(s)")


def find_target(addr, seconds=10):
    """Inquire until addr answers; returns its page scan repetition mode and clock offset."""
    command(0x0401, bytes([0x33, 0x8B, 0x9E, max(1, int(seconds / 1.28)), 0]))
    deadline = time.time() + seconds + 3
    while time.time() < deadline:
        ev = read_event(500)
        if not ev:
            continue
        code, p = ev
        if code == 0x01:
            break
        if code in (0x02, 0x22, 0x2F) and len(p) >= 15 and addr_str(p[1:7]) == addr.upper():
            psrm = p[7]
            clock = struct.unpack_from("<H", p, 13 if code == 0x02 else 12)[0]
            command(0x0402)  # Inquiry_Cancel
            print(f"connect: found {addr} (page scan mode {psrm}, clock offset {clock:#06x})", flush=True)
            return psrm, clock | 0x8000
    return 0x01, 0


def do_connect(addr):
    command(0x0C03)
    psrm, clock = find_target(addr)
    params = addr_bytes(addr) + struct.pack("<HBBHB", 0xCC18, psrm, 0x00, clock, 0x01)
    st = command(0x0405, params)  # Create_Connection
    print(f"connect: command status {st[0]:#04x}", flush=True)
    deadline = time.time() + 15
    while time.time() < deadline:
        ev = read_event(500)
        if not ev:
            continue
        code, p = ev
        if code == 0x03:  # Connection Complete
            status, handle = p[0], struct.unpack_from("<H", p, 1)[0] & 0x0FFF
            print(f"connect: complete status {status:#04x} handle {handle:#05x}")
            if status == 0:
                time.sleep(2)
                command(0x0406, struct.pack("<HB", handle, 0x13))  # Disconnect
                print("connect: link closed again")
            return
        print(f"  event {code:#04x}: {p.hex(' ')}")
    print("connect: no Connection Complete within 15s")


def do_rtlfw(fw_path, config_path):
    """Same steps as Linux btrtl for the original "Realtech" file format."""
    fw = open(fw_path, "rb").read()
    config = open(config_path, "rb").read() if config_path else b""
    if fw[:8] != b"Realtech":
        sys.exit("not a Realtech v1 firmware file")
    fw_version, num = struct.unpack_from("<IH", fw, 8)
    chip_ids = struct.unpack_from(f"<{num}H", fw, 14)
    lengths = struct.unpack_from(f"<{num}H", fw, 14 + 2 * num)
    offsets = struct.unpack_from(f"<{num}I", fw, 14 + 4 * num)
    command(0x0C03)
    rom = command(0xFC6D)[1]
    want = rom + 1
    if want not in chip_ids:
        sys.exit(f"no patch for ROM version {rom} (patches for chip ids {chip_ids})")
    i = chip_ids.index(want)
    patch = bytearray(fw[offsets[i]:offsets[i] + lengths[i]])
    patch[-4:] = struct.pack("<I", fw_version)  # the last word carries the firmware version
    data = bytes(patch) + config
    print(f"rtlfw: fw version {fw_version:#010x}, ROM {rom}, patch {i} ({lengths[i]} bytes) + config {len(config)} bytes")
    frags = (len(data) + 251) // 252
    for n in range(frags):
        index = n if n <= 0x7F else (n & 0x7F) + 1
        if n == frags - 1:
            index |= 0x80
        r = command(0xFC20, bytes([index]) + data[n * 252:(n + 1) * 252], timeout_ms=3000)  # Realtek download
        if r[0] != 0:
            sys.exit(f"rtlfw: fragment {n} failed, status {r[0]:#04x}")
    v = command(0x1001)
    print(f"rtlfw: {frags} fragments sent; LMP subversion now {struct.unpack_from('<H', v, 7)[0]:#06x}")


try:
    if cmd == "rtlfw":
        do_rtlfw(args[0], args[1] if len(args) > 1 else None)
    elif cmd == "info":
        do_info()
    elif cmd == "inquiry":
        do_inquiry(float(args[0]) if args else 10)
    elif cmd == "connect":
        do_connect(args[0])
    else:
        sys.exit(__doc__)
finally:
    usb.util.release_interface(dev, 0)
    usb.util.dispose_resources(dev)
