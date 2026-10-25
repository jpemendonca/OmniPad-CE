# Bluetooth HID host

These files come from AnyPad-PS5 by elmonomalvad0, through the BearTS fork (codeberg.org/bearts/Any-Pad-ps5), as brought into OmniPad by upstream pull request 18 (diegobarbosaa/OmniPad-PS5, commit `3b745d0`). They are GPL-3.0-or-later, the same as OmniPad.

Copied from there:

- `host.c`, `host.h` and `host_int.h`: the classic Bluetooth host. It handles inquiry, pairing, link keys, L2CAP, SDP and the HID channels.
- `le.c`, `smp_crypto.c` and `smp_crypto.h`: Bluetooth LE pads.
- `bt_profiles.c` and `bt_profiles.h`: the per-pad profiles. Upstream calls them `profiles.c` and `profiles.h`. They were renamed so they don't clash with OmniPad's USB `src/profiles.h`.
- `generic.c`: the generic HID profile.
- `crc32.c`, `crc32.h` and `pad.h`.

The upstream stack shares the PS5's internal radio with the system. Here it runs on OmniPad's own USB adapter instead: see `src/bt_adapter.c`, which provides the `hci_ops` transport. The optional sharing hooks (`yield`, `event_reads`, `acl_reads`) are left out, so the host keeps every read. The only change in these files is the profiles rename.
