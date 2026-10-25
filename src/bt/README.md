# Bluetooth HID host

These files come from AnyPad-PS5 by elmonomalvad0, through the BearTS fork (codeberg.org/bearts/Any-Pad-ps5), as brought into OmniPad by upstream pull request 18 (diegobarbosaa/OmniPad-PS5, commit `3b745d0`). They are GPL-3.0-or-later, the same as OmniPad.

Copied from there:

- `host.c`, `host.h` and `host_int.h`: the classic Bluetooth host. It handles inquiry, pairing, link keys, L2CAP, SDP and the HID channels.
- `le.c`, `smp_crypto.c` and `smp_crypto.h`: Bluetooth LE pads.
- `bt_profiles.c` and `bt_profiles.h`: the per-pad profiles. Upstream calls them `profiles.c` and `profiles.h`. They were renamed so they don't clash with OmniPad's USB `src/profiles.h`.
- `generic.c`: the generic HID profile.
- `crc32.c`, `crc32.h` and `pad.h`.

The upstream stack shares the PS5's internal radio with the system. Here it runs on OmniPad's own USB adapter instead: see `src/bt_adapter.c`, which provides the `hci_ops` transport. The optional sharing hooks (`yield`, `event_reads`, `acl_reads`) are left out, so the host keeps every read. Changes in these files, besides the profiles rename:

- `host_int.h`: `T_SELF_AUTH` and `T_SELF_ENCRYPT` are 0. A DS4 that calls back dropped the link about 170 ms after it came up, long before the old 800 ms wait ran out.
- `host.c`: a failed authentication on reconnect only forgets the key when the pad refused it (status 0x05 or 0x06). A collision or a timeout keeps the key.
- `host.c`: `host_pair` only logs searches of 30 s or more. The short background searches from `bt_adapter.c` would fill the log.
