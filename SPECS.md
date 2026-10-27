# SPECS.md

## Product

OmniPad CE runs as a payload on a jailbroken PS5 (FW 7.00 – 13.60). Third-party controllers become virtual DualSense pads in 3 slots, for players 2 to 4. Player 1 is the official DualSense, which the system handles.

In scope:
- USB pads and 2.4 GHz dongles, through one driver per model.
- Bluetooth pads through a separate USB Bluetooth adapter.
- A dashboard on port 8095, also reachable from a Media tab tile.
- PS5 notifications in Portuguese or English.

Out of scope:
- The console's internal Bluetooth radio.
- DualSense haptics and adaptive triggers on other pads.
- The touchpad as a surface (it is a single click).
- The Microsoft Xbox Wireless Adapter.

## Architecture

One process, with these threads:

| Thread | Code | Does |
|---|---|---|
| main | `main.c` | 250 Hz loop: `vpad_poll` (slot state machine, input injection), `usb_hotplug_poll` (scan), `tcp_stream_poll`, stop flag |
| USB readers | `usb_hotplug.c` | One per claimed USB pad. Keeps one IN transfer queued, parses through its driver, calls `vpad_update` |
| Bluetooth worker | `bt_adapter.c` | Scans every 2 s for an adapter, brings it up, runs the host (`src/bt/host.c`) with `host_poll` every 10 ms, maps host pads to slots |
| web | `web.c` | HTTP server for the dashboard and API |

Shared state goes through `ps5_vpad.c` (`g_vpad_mutex`) and small mutex-guarded structs (`g_bt.lock`). The vendored host is not thread-safe: only the Bluetooth worker calls it. Other threads leave requests (`pair_req`, `forget_req`) for the worker to pick up.

### Controllers

- USB: `pad_drivers.c` holds one entry per model: VID:PID, name, connection type, idle-release time, flags (`PADDRV_GIP`, `PADDRV_X360_KEEPALIVE`, `PADDRV_XINPUT_LED_OUT`) and parser. Pads with an XInput interface and no exact entry get a class driver. A new USB pad is a new table entry, not a change to shared code.
- Bluetooth: the host's profiles (`src/bt/bt_profiles.c`, `generic.c`) cover DualShock 4, DualSense, Switch Pro, single Joy-Cons, Wii U Pro and generic HID. A pad connects in one of two ways. In pairing mode, it is found by the background search or the Pair button. Once paired, it calls the adapter itself when PS is pressed.

### Bluetooth adapter

- Any USB device with an HCI interface (`E0/01/01`) is used, unless its vendor is MediaTek (`0e8d`, the console's radio). The vendor is checked before any other request reaches the device.
- Bring-up sequence:
  1. HCI reset.
  2. Read the version.
  3. If the chip is a Realtek RTL8761B still on its ROM (subversion `0x8761`), load the linux-firmware patch (`bt_rtl.c`, vendor command `0xFC20`, 252-byte fragments).
  4. Reset again.
  5. Set the class of device and the name "OmniPad", turn on extended inquiry results, then `host_open`.
- Transport: the event endpoint (interrupt) and ACL (bulk) each keep one read queued. Events and ACL packets are rebuilt from their headers.
- Background search, with no pad connected: it searches all the time. With one connected, it looks for 3.84 s every 20 s. With every slot taken, it does not search.
- Chip quirks: a CSR8510 clone (`0a12:0001` with a manufacturer other than `0x000A`) gets no background search while a pad is connected.
- Recovery: if the adapter locks up and is still on its node with the same ids, it gets a USB reset (`USB_DEVICEENUMERATE`), up to 2 times, before the user is asked to replug it.

## Data on the console

No network service or account is needed. The firmware is embedded at build time.

| Path on the PS5 | What |
|---|---|
| `/data/anypad/anypad.log` | Log, appended across runs. The path is kept from upstream. |
| `/data/anypad/omnipad.pid`, `/data/anypad/stop` | Single-instance control |
| `/data/anypad/omnipad_bt.db` | Bluetooth link keys. It is not PR 18's `pads.db`, whose keys belong to the internal radio. |
| `/data/anypad/maps/` | Per-pad Bluetooth profile settings |
| `/user/app/OMNP00001/sce_sys/` | Media tab tile: `param.json` (category 65536, `deeplinkUri` `http://127.0.0.1:8095/?tv=1`) and `icon0.png` |

## Contracts

HTTP on port 8095 (`web.c`):

| Route | Does |
|---|---|
| `GET /` | Dashboard. `?tv=1` switches to the TV layout. |
| `GET /api/state` | Version, uptime, per slot (status, name, conn, vid/pid, buttons, sticks, triggers, counters, idle release), and `bt` (state, adapter, address, pairing_left, paired, pads) |
| `GET /api/status` | Older per-slot summary |
| `GET /api/log[?since=N]` | Up to 32 KB of the log. `X-Log-Offset` says where the next read starts. |
| `POST /api/pair?seconds=N` | Bluetooth search for N s. `0` stops it. |
| `POST /api/bt_forget` | Forget every paired Bluetooth pad |
| `POST /api/press_ps?slot=N` | Simulate PS on a slot |
| `POST /api/disconnect?slot=N` | Free a slot; `-1` frees all |
| `POST /api/exit` | Stop the payload |

Notifications (`notify_ps5`, text through `ui_text`): started (with the IP), pad connected/disconnected (player = slot + 2), adapter ready or failed. Portuguese when the system language is 7 or 17, English otherwise.

## Tests

The policy is to test the main path only.
- Host tests (`make test`) cover the logic the main path depends on that runs without a PS5: report parsers and drivers, the virtual pad ABI, Realtek firmware selection, and the SDP server.
- New tests are added only for logic on the main path, or for a bug that broke it.
- Hardware behaviour is validated by the maintainer on a PS5. Commit messages and release notes say which parts were validated where.

## Secrets and config

There are none. No `.env`, keys or accounts. The only external input is the Realtek firmware, which `tools/get-bt-firmware.sh` downloads into `firmware/` (gitignored). The build embeds it when it is present.

## Releases

GitHub releases on `jpemendonca/OmniPad-CE`. Assets: `OmniPad-CE.elf` and the Realtek licence file. A release is published only on the maintainer's explicit request. The steps are in `AGENTS.md`.

## Decisions

- **2026-10-22: one IN transfer kept queued per USB pad.** Restarting a timed-out transfer every 15 ms lost presses from pads that report only on change.
- **2026-10-22: the 8BitDo Ultimate 2C needs the XInput player-LED command (`01 03 02`) on its OUT endpoint** before it reports.
- **2026-10-24: one driver per controller model** (`pad_drivers.c`), so that fixing one pad cannot break another.
- **2026-10-24: Bluetooth through OmniPad's own USB adapter.** Sharing the internal radio, as AnyPad and PR 18 do, left the DualSense stuck until a restart. Injecting into `SceRemotePlay` was dropped.
- **2026-10-25: the AnyPad host was vendored** (`src/bt/`) rather than rewritten. Three changes were needed for a dedicated adapter, and they are listed in `src/bt/README.md`:
  - authentication at once on reconnect, since the DS4 drops a link after ~170 ms;
  - an SDP server, since the DS4 hangs up when its SDP request is refused;
  - a key is forgotten only when the pad refuses it.
- **2026-10-25: the adapter searches by itself.** The user only puts the pad in pairing mode, with no dashboard needed.
- **2026-10-27: Media tab tile** with the same mechanism as ps5-web-file-manager: an app with no executable, plus a `deeplinkUri`.
- **2026-10-27: notifications follow the system language**, Portuguese or English.
- **2026-10-27: CSR8510 clones get no background search while linked, and locked-up adapters get a USB reset.** A clone locked up 25 s after a DS4 connected.
- **2026-10-27: renamed to OmniPad CE**, the community continuation. Upstream has been inactive since early October 2026.
